#!/usr/bin/env python3
import argparse
import json
import os
import queue
import re
import signal
import sys
import termios
import threading
import time
from collections import deque
from dataclasses import dataclass, field
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse


@dataclass
class BridgeState:
    board_id: str
    serial_device: str
    baudrate: int
    boot_time: float = field(default_factory=time.time)
    connected: bool = False
    last_seen: float | None = None
    last_line: str | None = None
    alive_counter: int | None = None
    bytes_received: int = 0
    bytes_sent: int = 0
    lines_received: int = 0
    error: str | None = None
    controls: dict[str, dict[str, str]] = field(default_factory=dict)
    sensors: dict[str, bool] = field(default_factory=dict)
    log: deque[dict[str, Any]] = field(default_factory=lambda: deque(maxlen=200))
    lock: threading.Lock = field(default_factory=threading.Lock)

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            return {
                "boardId": self.board_id,
                "serialDevice": self.serial_device,
                "baudrate": self.baudrate,
                "connected": self.connected,
                "lastSeen": self.last_seen,
                "lastLine": self.last_line,
                "aliveCounter": self.alive_counter,
                "bytesReceived": self.bytes_received,
                "bytesSent": self.bytes_sent,
                "linesReceived": self.lines_received,
                "error": self.error,
                "controls": dict(self.controls),
                "sensors": dict(self.sensors),
                "uptimeSeconds": round(time.time() - self.boot_time, 3),
            }

    def append_log(self, direction: str, payload: str) -> None:
        with self.lock:
            self.log.append(
                {
                    "ts": time.time(),
                    "direction": direction,
                    "payload": payload,
                }
            )

    def update_rx(self, line: str, raw_len: int) -> None:
        with self.lock:
            self.connected = True
            self.last_seen = time.time()
            self.last_line = line
            self.bytes_received += raw_len
            self.lines_received += 1
            if line.startswith("alive "):
                try:
                    self.alive_counter = int(line.split()[1])
                except (IndexError, ValueError):
                    pass
            state_match = re.match(
                r"^state\s+(pump|misc)\s+([a-z0-9_]+)\s+mode\s+(auto|manual)\s+output\s+(on|off)$",
                line,
                re.IGNORECASE,
            )
            if state_match:
                group, key, mode, output = state_match.groups()
                self.controls[f"{group.lower()}.{key.lower()}"] = {
                    "group": group.lower(),
                    "key": key.lower(),
                    "mode": mode.lower(),
                    "state": output.lower(),
                }
            sensor_match = re.match(r"^sensor\s+([a-z0-9_]+)\s+(wet|dry)$", line, re.IGNORECASE)
            if sensor_match:
                key, wet = sensor_match.groups()
                self.sensors[key.lower()] = wet.lower() == "wet"
            self.log.append(
                {
                    "ts": self.last_seen,
                    "direction": "rx",
                    "payload": line,
                }
            )
            self.error = None

    def update_tx(self, payload: str, raw_len: int) -> None:
        with self.lock:
            self.bytes_sent += raw_len
            self.log.append(
                {
                    "ts": time.time(),
                    "direction": "tx",
                    "payload": payload,
                }
            )

    def set_error(self, error: str) -> None:
        with self.lock:
            self.connected = False
            self.error = error

    def recent_log(self, limit: int) -> list[dict[str, Any]]:
        with self.lock:
            return list(self.log)[-limit:]


def configure_serial(fd: int, baudrate: int) -> None:
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CLOCAL | termios.CREAD | termios.CS8
    attrs[3] = 0

    speed_map = {
        9600: termios.B9600,
        19200: termios.B19200,
        38400: termios.B38400,
        57600: termios.B57600,
        115200: termios.B115200,
        230400: termios.B230400,
    }
    speed = speed_map.get(baudrate)
    if speed is None:
        raise ValueError(f"unsupported baudrate: {baudrate}")

    attrs[4] = speed
    attrs[5] = speed
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, attrs)


class SerialBridge:
    def __init__(self, device: str, baudrate: int, state: BridgeState) -> None:
        self.device = device
        self.baudrate = baudrate
        self.state = state
        self.stop_event = threading.Event()
        self.fd: int | None = None
        self.thread: threading.Thread | None = None
        self.write_lock = threading.Lock()

    def start(self) -> None:
        self.thread = threading.Thread(target=self._run, name="serial-bridge", daemon=True)
        self.thread.start()

    def stop(self) -> None:
        self.stop_event.set()
        if self.thread is not None:
            self.thread.join(timeout=3)
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def _close_fd(self) -> None:
        if self.fd is not None:
            try:
                os.close(self.fd)
            except OSError:
                pass
            self.fd = None

    def send_text(self, text: str) -> None:
        payload = text.encode("utf-8")
        if not payload.endswith(b"\n"):
            payload += b"\n"
        if self.fd is None:
            raise RuntimeError("serial device is not open")
        try:
            with self.write_lock:
                written = os.write(self.fd, payload)
        except OSError as exc:
            self.state.set_error(f"write failed: {exc}")
            self._close_fd()
            raise RuntimeError(f"serial write failed: {exc}") from exc
        self.state.update_tx(payload.decode("utf-8", errors="replace").rstrip("\r\n"), written)

    def _open(self) -> int:
        fd = os.open(self.device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        configure_serial(fd, self.baudrate)
        return fd

    def _run(self) -> None:
        buffer = bytearray()
        while not self.stop_event.is_set():
            if self.fd is None:
                try:
                    self.fd = self._open()
                    self.state.connected = True
                    self.state.error = None
                except OSError as exc:
                    self.state.set_error(f"open failed: {exc}")
                    time.sleep(1)
                    continue
                except ValueError as exc:
                    self.state.set_error(str(exc))
                    return

            try:
                chunk = os.read(self.fd, 1024)
            except BlockingIOError:
                time.sleep(0.05)
                continue
            except OSError as exc:
                self.state.set_error(f"read failed: {exc}")
                self._close_fd()
                time.sleep(1)
                continue

            if not chunk:
                time.sleep(0.05)
                continue

            buffer.extend(chunk)
            while b"\n" in buffer:
                raw_line, _, buffer = buffer.partition(b"\n")
                line = raw_line.decode("utf-8", errors="replace").rstrip("\r")
                self.state.update_rx(line, len(raw_line) + 1)


class SseHub:
    def __init__(self) -> None:
        self.clients: set[queue.Queue[str]] = set()
        self.lock = threading.Lock()

    def subscribe(self) -> queue.Queue[str]:
        client: queue.Queue[str] = queue.Queue(maxsize=32)
        with self.lock:
            self.clients.add(client)
        return client

    def unsubscribe(self, client: queue.Queue[str]) -> None:
        with self.lock:
            self.clients.discard(client)

    def publish(self, event: dict[str, Any]) -> None:
        payload = f"data: {json.dumps(event, ensure_ascii=True)}\n\n"
        with self.lock:
            clients = list(self.clients)
        for client in clients:
            try:
                client.put_nowait(payload)
            except queue.Full:
                pass


def make_handler(state: BridgeState, bridge: SerialBridge, sse_hub: SseHub):
    board_segments = state.board_id.split("-")
    board_path = "/api/" + "/".join(board_segments)

    class Handler(BaseHTTPRequestHandler):
        server_version = "STM32Bridge/0.1"

        def do_GET(self) -> None:
            parsed = urlparse(self.path)
            if parsed.path == "/health":
                self._json(HTTPStatus.OK, {"ok": True, "state": state.snapshot()})
                return
            if parsed.path == f"{board_path}/status":
                self._json(HTTPStatus.OK, state.snapshot())
                return
            if parsed.path == f"{board_path}/logs":
                params = parse_qs(parsed.query)
                limit = max(1, min(int(params.get("limit", ["50"])[0]), 200))
                self._json(HTTPStatus.OK, {"items": state.recent_log(limit)})
                return
            if parsed.path == f"{board_path}/events":
                self._sse()
                return
            self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})

        def do_POST(self) -> None:
            parsed = urlparse(self.path)
            if parsed.path != f"{board_path}/send":
                self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})
                return
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length)
            try:
                payload = json.loads(raw.decode("utf-8"))
                text = str(payload["text"])
            except (json.JSONDecodeError, KeyError, UnicodeDecodeError):
                self._json(HTTPStatus.BAD_REQUEST, {"error": "expected JSON body with text"})
                return
            try:
                bridge.send_text(text)
            except Exception as exc:
                self._json(HTTPStatus.BAD_GATEWAY, {"error": str(exc)})
                return
            sse_hub.publish({"type": "tx", "payload": text, "ts": time.time()})
            self._json(HTTPStatus.OK, {"ok": True, "sent": text})

        def log_message(self, format: str, *args: Any) -> None:
            sys.stdout.write(f"[http] {self.address_string()} - {format % args}\n")

        def _json(self, status: HTTPStatus, payload: dict[str, Any]) -> None:
            body = json.dumps(payload, ensure_ascii=True).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _sse(self) -> None:
            client = sse_hub.subscribe()
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            try:
                self.wfile.write(f"data: {json.dumps({'type': 'snapshot', 'state': state.snapshot()})}\n\n".encode("utf-8"))
                self.wfile.flush()
                while True:
                    try:
                        message = client.get(timeout=15)
                    except queue.Empty:
                        message = "event: ping\ndata: {}\n\n"
                    self.wfile.write(message.encode("utf-8"))
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                pass
            finally:
                sse_hub.unsubscribe(client)

    return Handler


def start_publisher(state: BridgeState, sse_hub: SseHub, stop_event: threading.Event) -> threading.Thread:
    def _run() -> None:
        last_seen_line = None
        last_snapshot = ""
        while not stop_event.is_set():
            snapshot = state.snapshot()
            current_line = snapshot["lastLine"]
            if current_line and current_line != last_seen_line:
                sse_hub.publish({"type": "rx", "payload": current_line, "ts": snapshot["lastSeen"]})
                last_seen_line = current_line
            encoded_snapshot = json.dumps(snapshot, sort_keys=True, ensure_ascii=True)
            if encoded_snapshot != last_snapshot:
                sse_hub.publish({"type": "snapshot", "state": snapshot})
                last_snapshot = encoded_snapshot
            time.sleep(0.1)

    thread = threading.Thread(target=_run, name="sse-publisher", daemon=True)
    thread.start()
    return thread


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Ubuntu serial bridge for STM32 #01")
    parser.add_argument("--board-id", default="stm32-01")
    parser.add_argument("--device", default="/dev/ttyUSB0")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8081)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    state = BridgeState(board_id=args.board_id, serial_device=args.device, baudrate=args.baudrate)
    bridge = SerialBridge(args.device, args.baudrate, state)
    sse_hub = SseHub()
    stop_event = threading.Event()

    def _shutdown(signum: int, frame: Any) -> None:
        del signum, frame
        stop_event.set()

    signal.signal(signal.SIGINT, _shutdown)
    signal.signal(signal.SIGTERM, _shutdown)

    bridge.start()
    start_publisher(state, sse_hub, stop_event)

    server = ThreadingHTTPServer((args.host, args.port), make_handler(state, bridge, sse_hub))
    server.timeout = 0.5

    print(
        f"stm32 bridge listening on http://{args.host}:{args.port} for "
        f"{args.board_id} at {args.device} @ {args.baudrate}"
    )
    try:
        while not stop_event.is_set():
            server.handle_request()
    finally:
        server.server_close()
        bridge.stop()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
