#!/usr/bin/env python3
import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass, field
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse

from event_store import EventStore
from stm32_bridge import BridgeState, SerialBridge, SseHub as SerialSseHub, start_publisher
from webos_tv_bridge import SseHub as TvSseHub
from webos_tv_bridge import TvState, WebOsTvBridge, start_command_worker


TV_ROUTE_PREFIX = "/api/tv/living-room"
ROOM_NODE_CONFIGS = [
    ("/api/node/living-room-01", "living-room-node-01"),
    ("/api/node/living-room-02", "living-room-node-02"),
]
TV_STALE_AFTER_SECONDS = 20.0


@dataclass
class Stm32Runtime:
    route_prefix: str
    state: BridgeState
    bridge: SerialBridge
    sse_hub: SerialSseHub
    event_store: EventStore | None = None


@dataclass
class RoomNodeState:
    node_id: str
    broker_host: str
    broker_port: int
    boot_time: float = field(default_factory=time.time)
    connected: bool = False
    availability: str = "unknown"
    last_seen: float | None = None
    last_event: str | None = None
    last_error: str | None = None
    ip: str | None = None
    wifi_rssi: int | None = None
    free_heap: int | None = None
    relays: dict[str, bool] = field(default_factory=dict)
    touches: dict[str, bool] = field(default_factory=dict)
    led_mode: str | None = None
    log: deque[dict[str, Any]] = field(default_factory=lambda: deque(maxlen=100))
    lock: threading.Lock = field(default_factory=threading.Lock)

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            return {
                "nodeId": self.node_id,
                "brokerHost": self.broker_host,
                "brokerPort": self.broker_port,
                "connected": self.connected,
                "availability": self.availability,
                "lastSeen": self.last_seen,
                "lastEvent": self.last_event,
                "lastError": self.last_error,
                "ip": self.ip,
                "wifiRssi": self.wifi_rssi,
                "freeHeap": self.free_heap,
                "relays": dict(self.relays),
                "touches": dict(self.touches),
                "ledMode": self.led_mode,
                "uptimeSeconds": round(time.time() - self.boot_time, 3),
            }

    def append_log(self, direction: str, topic: str, payload: str) -> None:
        with self.lock:
            ts = time.time()
            self.log.append({"ts": ts, "direction": direction, "topic": topic, "payload": payload})

    def recent_log(self, limit: int) -> list[dict[str, Any]]:
        with self.lock:
            return list(self.log)[-limit:]


@dataclass
class RoomNodeRuntime:
    route_prefix: str
    state: RoomNodeState
    sse_hub: SerialSseHub
    stop_event: threading.Event
    broker_host: str
    broker_port: int
    command_topic: str
    sub_topics: list[str]
    process: subprocess.Popen[str] | None = None
    thread: threading.Thread | None = None
    event_store: EventStore | None = None

    def start(self) -> None:
        self.thread = threading.Thread(target=self._run, name=f"{self.state.node_id}-mqtt", daemon=True)
        self.thread.start()

    def stop(self) -> None:
        self.stop_event.set()
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
        if self.thread is not None:
            self.thread.join(timeout=3)

    def send_command(self, payload: dict[str, Any]) -> None:
        encoded = json.dumps(payload, ensure_ascii=True)
        subprocess.run(
            [
                "mosquitto_pub",
                "-h",
                self.broker_host,
                "-p",
                str(self.broker_port),
                "-t",
                self.command_topic,
                "-m",
                encoded,
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        self.state.append_log("tx", self.command_topic, encoded)
        if self.event_store is not None:
            self.event_store.record(
                source_type="room_node",
                source_id=self.state.node_id,
                event_type="command",
                direction="tx",
                topic=self.command_topic,
                payload_text=encoded,
                payload_json=payload,
                state=self.state.snapshot(),
            )
        self.sse_hub.publish({"type": "tx", "topic": self.command_topic, "payload": encoded, "ts": time.time()})

    def _apply_topic_payload(self, topic: str, payload: str) -> None:
        now = time.time()
        try:
            if topic.endswith("/availability"):
                with self.state.lock:
                    self.state.availability = payload.strip()
                    self.state.connected = payload.strip() == "online"
                    self.state.last_seen = now
                    self.state.last_error = None
            elif topic.endswith("/telemetry") or topic.endswith("/state"):
                data = json.loads(payload)
                with self.state.lock:
                    self.state.last_seen = now
                    self.state.last_error = None
                    self.state.connected = True
                    self.state.availability = "online"
                    self.state.last_event = data.get("event", self.state.last_event)
                    self.state.ip = data.get("ip", self.state.ip)
                    self.state.wifi_rssi = data.get("wifiRssi", self.state.wifi_rssi)
                    self.state.free_heap = data.get("freeHeap", self.state.free_heap)
                    self.state.led_mode = data.get("ledMode", self.state.led_mode)
                    if "relay" in data:
                        self.state.relays["relay"] = bool(data.get("relay"))
                    if "touch" in data:
                        self.state.touches["touch"] = bool(data.get("touch"))
                    for item in data.get("relays", []):
                        key = str(item.get("key", ""))
                        if not key:
                            continue
                        self.state.relays[key] = bool(item.get("on"))
                        if "touchActive" in item:
                            self.state.touches[key] = bool(item.get("touchActive"))
        except Exception as exc:
            with self.state.lock:
                self.state.last_error = str(exc)
        self.state.append_log("rx", topic, payload)
        if self.event_store is not None:
            self.event_store.record(
                source_type="room_node",
                source_id=self.state.node_id,
                event_type="mqtt",
                direction="rx",
                topic=topic,
                payload_text=payload,
                state=self.state.snapshot(),
            )
        self.sse_hub.publish({"type": "snapshot", "state": self.state.snapshot()})
        self.sse_hub.publish({"type": "rx", "topic": topic, "payload": payload, "ts": now})

    def _run(self) -> None:
        while not self.stop_event.is_set():
            cmd = ["mosquitto_sub", "-h", self.broker_host, "-p", str(self.broker_port), "-v"]
            for topic in self.sub_topics:
                cmd.extend(["-t", topic])
            try:
                self.process = subprocess.Popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    bufsize=1,
                )
            except Exception as exc:
                with self.state.lock:
                    self.state.connected = False
                    self.state.last_error = str(exc)
                time.sleep(2)
                continue

            try:
                assert self.process.stdout is not None
                for line in self.process.stdout:
                    if self.stop_event.is_set():
                        break
                    line = line.rstrip("\r\n")
                    if not line or " " not in line:
                        continue
                    topic, payload = line.split(" ", 1)
                    self._apply_topic_payload(topic, payload)
            finally:
                if self.process is not None and self.process.poll() is None:
                    self.process.terminate()
                with self.state.lock:
                    self.state.connected = False
                    if not self.state.last_error:
                        self.state.last_error = "mqtt subscription stopped"
                if not self.stop_event.is_set():
                    time.sleep(1)


class SmartHomeApiServer(ThreadingHTTPServer):
    daemon_threads = True
    block_on_close = False


def build_tv_snapshot(state: TvState) -> dict[str, Any]:
    snapshot = state.snapshot()
    last_seen = snapshot.get("lastSeen")
    snapshot["stale"] = bool(last_seen and time.time() - last_seen > TV_STALE_AFTER_SECONDS)
    return snapshot


def start_integrated_tv_probe_loop(
    bridge: WebOsTvBridge,
    state: TvState,
    sse_hub: TvSseHub,
    stop_event: threading.Event,
    event_store: EventStore | None = None,
) -> threading.Thread:
    def _stable_snapshot(snapshot: dict[str, Any]) -> dict[str, Any]:
        return {key: value for key, value in snapshot.items() if key != "uptimeSeconds"}

    def _run() -> None:
        last_snapshot = ""
        while not stop_event.is_set():
            try:
                if bridge.io_lock.locked():
                    time.sleep(0.05)
                    continue

                probe_result = bridge.probe(timeout=0.6)
                now = time.time()
                reachable = probe_result["port3000"] or probe_result["port3001"]

                if (
                    bridge.client_key
                    and reachable
                    and bridge.command_queue.empty()
                    and now - bridge.last_command_at >= 1.5
                    and now - bridge.last_refresh_at >= 5.0
                    and not bridge.io_lock.locked()
                ):
                    try:
                        bridge.refresh()
                    except Exception as exc:
                        with state.lock:
                            state.last_error = str(exc)
                            # Keep cached fields for a grace period, but do not pretend the TV is still reachable.
                            state.reachable = False
                            if not state.last_seen or now - state.last_seen > TV_STALE_AFTER_SECONDS:
                                state.wake_pending = False
                elif not reachable:
                    with state.lock:
                        state.reachable = False
                        if not state.last_seen or now - state.last_seen > TV_STALE_AFTER_SECONDS:
                            state.wake_pending = False
            except Exception as exc:
                now = time.time()
                with state.lock:
                    state.last_error = str(exc)
                    state.reachable = False
                    if not state.last_seen or now - state.last_seen > TV_STALE_AFTER_SECONDS:
                        state.wake_pending = False

            snapshot = build_tv_snapshot(state)
            encoded = json.dumps(_stable_snapshot(snapshot), sort_keys=True, ensure_ascii=True)
            if encoded != last_snapshot:
                sse_hub.publish({"type": "snapshot", "state": snapshot})
                if event_store is not None:
                    event_store.record(
                        source_type="tv",
                        source_id=state.tv_id,
                        event_type="snapshot",
                        state=snapshot,
                    )
                last_snapshot = encoded
            time.sleep(0.75)

    thread = threading.Thread(target=_run, name="integrated-tv-probe", daemon=True)
    thread.start()
    return thread


class Handler(BaseHTTPRequestHandler):
    server_version = "SmartHomeApi/0.3"
    tv_bridge: WebOsTvBridge | None = None
    tv_state: TvState | None = None
    tv_sse_hub: TvSseHub | None = None
    stm32_runtimes: dict[str, Stm32Runtime] = {}
    room_node_runtimes: dict[str, RoomNodeRuntime] = {}
    event_store: EventStore | None = None

    def do_OPTIONS(self) -> None:
        self.send_response(HTTPStatus.NO_CONTENT)
        self._write_common_headers(content_type=None, content_length=None)
        self.end_headers()

    def do_GET(self) -> None:
        self._handle_request()

    def do_POST(self) -> None:
        self._handle_request()

    def log_message(self, format: str, *args: Any) -> None:
        sys.stdout.write(f"[smart-home-api] {self.address_string()} - {format % args}\n")

    def _handle_request(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path == "/health":
            self._json(HTTPStatus.OK, self._backend_health())
            return
        if parsed.path == "/api/history":
            self._history()
            return

        if parsed.path.startswith(TV_ROUTE_PREFIX):
            self._handle_tv_request(parsed)
            return

        room_node_runtime = self._resolve_room_node_runtime(parsed.path)
        if room_node_runtime is not None:
            self._handle_room_node_request(parsed, room_node_runtime)
            return

        runtime = self._resolve_stm32_runtime(parsed.path)
        if runtime is not None:
            self._handle_stm32_request(parsed, runtime)
            return

        self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})

    def _backend_health(self) -> dict[str, Any]:
        services: dict[str, Any] = {}
        for prefix, runtime in self.stm32_runtimes.items():
            services[prefix] = {
                "prefix": prefix,
                "host": "internal",
                "port": None,
                "ok": True,
                "status": HTTPStatus.OK,
                "payload": {"ok": True, "state": runtime.state.snapshot()},
            }

        tv_state = build_tv_snapshot(self._require_tv_state())
        services[TV_ROUTE_PREFIX] = {
            "prefix": TV_ROUTE_PREFIX,
            "host": tv_state["host"],
            "port": tv_state["port"],
            "ok": True,
            "status": HTTPStatus.OK,
            "payload": {"ok": True, "state": tv_state},
        }
        for prefix, runtime in self.room_node_runtimes.items():
            services[prefix] = {
                "prefix": prefix,
                "host": runtime.broker_host,
                "port": runtime.broker_port,
                "ok": True,
                "status": HTTPStatus.OK,
                "payload": {"ok": True, "state": runtime.state.snapshot()},
            }
        return {"ok": True, "services": services}

    def _resolve_stm32_runtime(self, path: str) -> Stm32Runtime | None:
        for prefix, runtime in self.stm32_runtimes.items():
            if path == prefix or path.startswith(f"{prefix}/"):
                return runtime
        return None

    def _resolve_room_node_runtime(self, path: str) -> RoomNodeRuntime | None:
        for prefix, runtime in self.room_node_runtimes.items():
            if path == prefix or path.startswith(f"{prefix}/"):
                return runtime
        return None

    def _require_tv_bridge(self) -> WebOsTvBridge:
        if self.tv_bridge is None:
            raise RuntimeError("TV bridge is not configured")
        return self.tv_bridge

    def _require_tv_state(self) -> TvState:
        if self.tv_state is None:
            raise RuntimeError("TV state is not configured")
        return self.tv_state

    def _require_tv_sse_hub(self) -> TvSseHub:
        if self.tv_sse_hub is None:
            raise RuntimeError("TV SSE hub is not configured")
        return self.tv_sse_hub

    def _require_event_store(self) -> EventStore:
        if self.event_store is None:
            raise RuntimeError("Event store is not configured")
        return self.event_store

    def _history(self) -> None:
        params = parse_qs(urlparse(self.path).query)
        limit = max(1, min(int(params.get("limit", ["100"])[0]), 1000))
        source_type = params.get("source_type", [None])[0]
        source_id = params.get("source_id", [None])[0]
        event_type = params.get("event_type", [None])[0]
        items = self._require_event_store().recent_events(
            limit=limit,
            source_type=source_type,
            source_id=source_id,
            event_type=event_type,
        )
        self._json(HTTPStatus.OK, {"items": items})

    def _handle_tv_request(self, parsed: Any) -> None:
        if parsed.path == f"{TV_ROUTE_PREFIX}/status":
            self._json(HTTPStatus.OK, build_tv_snapshot(self._require_tv_state()))
            return
        if parsed.path == f"{TV_ROUTE_PREFIX}/events":
            self._tv_sse()
            return
        if parsed.path == f"{TV_ROUTE_PREFIX}/pair":
            self._tv_action("pair")
            return
        if parsed.path == f"{TV_ROUTE_PREFIX}/refresh":
            self._tv_action("refresh")
            return
        if parsed.path == f"{TV_ROUTE_PREFIX}/command":
            self._tv_action("command")
            return
        self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})

    def _handle_room_node_request(self, parsed: Any, runtime: RoomNodeRuntime) -> None:
        if parsed.path == f"{runtime.route_prefix}/status":
            self._json(HTTPStatus.OK, runtime.state.snapshot())
            return
        if parsed.path == f"{runtime.route_prefix}/logs":
            params = parse_qs(parsed.query)
            limit = max(1, min(int(params.get("limit", ["50"])[0]), 100))
            self._json(HTTPStatus.OK, {"items": runtime.state.recent_log(limit)})
            return
        if parsed.path == f"{runtime.route_prefix}/events":
            self._room_node_sse(runtime)
            return
        if parsed.path == f"{runtime.route_prefix}/command":
            self._room_node_command(runtime)
            return
        self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})

    def _room_node_command(self, runtime: RoomNodeRuntime) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length else b"{}"
        try:
            body = json.loads(raw.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            self._json(HTTPStatus.BAD_REQUEST, {"error": "expected JSON body"})
            return
        try:
            runtime.send_command(body)
        except subprocess.CalledProcessError as exc:
            detail = exc.stderr.strip() if exc.stderr else str(exc)
            self._json(HTTPStatus.BAD_GATEWAY, {"error": detail, "state": runtime.state.snapshot()})
            return
        except Exception as exc:
            self._json(HTTPStatus.BAD_GATEWAY, {"error": str(exc), "state": runtime.state.snapshot()})
            return
        self._json(HTTPStatus.OK, {"ok": True, "sent": body, "state": runtime.state.snapshot()})

    def _room_node_sse(self, runtime: RoomNodeRuntime) -> None:
        client = runtime.sse_hub.subscribe()
        self.send_response(HTTPStatus.OK)
        self._write_common_headers(content_type="text/event-stream", content_length=None)
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        try:
            initial = f"data: {json.dumps({'type': 'snapshot', 'state': runtime.state.snapshot()}, ensure_ascii=True)}\n\n"
            self.wfile.write(initial.encode("utf-8"))
            self.wfile.flush()
            while True:
                try:
                    message = client.get(timeout=15)
                except Exception:
                    message = "event: ping\ndata: {}\n\n"
                self.wfile.write(message.encode("utf-8"))
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            runtime.sse_hub.unsubscribe(client)

    def _tv_action(self, action: str) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length else b"{}"
        try:
            body = json.loads(raw.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            self._json(HTTPStatus.BAD_REQUEST, {"error": "expected JSON body"})
            return

        try:
            bridge = self._require_tv_bridge()
            state = self._require_tv_state()
            hub = self._require_tv_sse_hub()
            if action == "pair":
                result = bridge.pair()
            elif action == "refresh":
                result = bridge.refresh()
            elif action == "command":
                result = bridge.send_command(body)
            else:
                self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})
                return
            if self.event_store is not None:
                self.event_store.record(
                    source_type="tv",
                    source_id=state.tv_id,
                    event_type=action,
                    direction="tx",
                    payload_json=body,
                    state=build_tv_snapshot(state),
                )
        except Exception as exc:
            self._json(HTTPStatus.BAD_GATEWAY, {"error": str(exc), "state": build_tv_snapshot(self._require_tv_state())})
            return

        self._json(HTTPStatus.OK, result)

    def _tv_sse(self) -> None:
        client = self._require_tv_sse_hub().subscribe()
        self.send_response(HTTPStatus.OK)
        self._write_common_headers(content_type="text/event-stream", content_length=None)
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        try:
            initial = f"data: {json.dumps({'type': 'snapshot', 'state': build_tv_snapshot(self._require_tv_state())}, ensure_ascii=True)}\n\n"
            self.wfile.write(initial.encode("utf-8"))
            self.wfile.flush()
            while True:
                try:
                    message = client.get(timeout=15)
                except Exception:
                    message = "event: ping\ndata: {}\n\n"
                self.wfile.write(message.encode("utf-8"))
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            self._require_tv_sse_hub().unsubscribe(client)

    def _handle_stm32_request(self, parsed: Any, runtime: Stm32Runtime) -> None:
        if parsed.path == f"{runtime.route_prefix}/status":
            self._json(HTTPStatus.OK, runtime.state.snapshot())
            return
        if parsed.path == f"{runtime.route_prefix}/logs":
            params = parse_qs(parsed.query)
            limit = max(1, min(int(params.get("limit", ["50"])[0]), 200))
            self._json(HTTPStatus.OK, {"items": runtime.state.recent_log(limit)})
            return
        if parsed.path == f"{runtime.route_prefix}/events":
            self._stm32_sse(runtime)
            return
        if parsed.path == f"{runtime.route_prefix}/send":
            self._stm32_send(runtime)
            return
        self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})

    def _stm32_send(self, runtime: Stm32Runtime) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length)
        try:
            payload = json.loads(raw.decode("utf-8"))
            text = str(payload["text"])
        except (json.JSONDecodeError, KeyError, UnicodeDecodeError):
            self._json(HTTPStatus.BAD_REQUEST, {"error": "expected JSON body with text"})
            return
        try:
            runtime.bridge.send_text(text)
        except Exception as exc:
            self._json(HTTPStatus.BAD_GATEWAY, {"error": str(exc)})
            return
        if runtime.event_store is not None:
            runtime.event_store.record(
                source_type="stm32",
                source_id=runtime.state.board_id,
                event_type="tx",
                direction="tx",
                payload_text=text,
                state=runtime.state.snapshot(),
            )
        runtime.sse_hub.publish({"type": "tx", "payload": text, "ts": time.time()})
        self._json(HTTPStatus.OK, {"ok": True, "sent": text})

    def _stm32_sse(self, runtime: Stm32Runtime) -> None:
        client = runtime.sse_hub.subscribe()
        self.send_response(HTTPStatus.OK)
        self._write_common_headers(content_type="text/event-stream", content_length=None)
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        try:
            initial = f"data: {json.dumps({'type': 'snapshot', 'state': runtime.state.snapshot()}, ensure_ascii=True)}\n\n"
            self.wfile.write(initial.encode("utf-8"))
            self.wfile.flush()
            while True:
                try:
                    message = client.get(timeout=15)
                except Exception:
                    message = "event: ping\ndata: {}\n\n"
                self.wfile.write(message.encode("utf-8"))
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            runtime.sse_hub.unsubscribe(client)

    def _json(self, status: HTTPStatus, payload: dict[str, Any]) -> None:
        body = json.dumps(payload, ensure_ascii=True).encode("utf-8")
        self.send_response(status)
        self._write_common_headers(
            content_type="application/json; charset=utf-8",
            content_length=len(body),
        )
        self.end_headers()
        self.wfile.write(body)

    def _write_common_headers(self, content_type: str | None, content_length: int | None) -> None:
        if content_type is not None:
            self.send_header("Content-Type", content_type)
        if content_length is not None:
            self.send_header("Content-Length", str(content_length))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Last-Event-ID")
        self.send_header("Access-Control-Allow-Private-Network", "true")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Smart Home API")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8090)
    parser.add_argument("--tv-host", default="192.168.1.52")
    parser.add_argument("--tv-port", type=int, default=3000)
    parser.add_argument("--tv-secure-port", type=int, default=3001)
    parser.add_argument(
        "--tv-client-key-path",
        default=str(Path(__file__).with_name("state") / "webos_living_room_tv.json"),
    )
    parser.add_argument(
        "--stm32-01-device",
        default="/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_BG01CM23-if00-port0",
    )
    parser.add_argument(
        "--stm32-02-device",
        default="/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_BG01OWDO-if00-port0",
    )
    parser.add_argument(
        "--stm32-03-device",
        default="/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_BG01OQ9Q-if00-port0",
    )
    parser.add_argument("--stm32-baudrate", type=int, default=115200)
    parser.add_argument("--mqtt-broker-host", default="127.0.0.1")
    parser.add_argument("--mqtt-broker-port", type=int, default=1883)
    parser.add_argument(
        "--event-db-path",
        default=str(Path(__file__).with_name("state") / "smart_home_events.sqlite3"),
    )
    return parser.parse_args()


def build_stm32_runtimes(
    args: argparse.Namespace,
    stop_event: threading.Event,
    event_store: EventStore | None = None,
) -> dict[str, Stm32Runtime]:
    configs = [
        ("/api/stm32/01", "stm32-01", args.stm32_01_device),
        ("/api/stm32/02", "stm32-02", args.stm32_02_device),
        ("/api/stm32/03", "stm32-03", args.stm32_03_device),
    ]
    runtimes: dict[str, Stm32Runtime] = {}
    for route_prefix, board_id, device in configs:
        state = BridgeState(board_id=board_id, serial_device=device, baudrate=args.stm32_baudrate)
        bridge = SerialBridge(device, args.stm32_baudrate, state)
        sse_hub = SerialSseHub()
        bridge.start()
        def _stm32_event_callback(event: dict[str, Any], snapshot: dict[str, Any], board_id: str = board_id) -> None:
            if event_store is None:
                return
            event_store.record(
                source_type="stm32",
                source_id=board_id,
                event_type=event["type"],
                direction="rx" if event["type"] == "rx" else None,
                payload_text=event.get("payload"),
                state=snapshot if event["type"] == "snapshot" else snapshot,
            )

        start_publisher(state, sse_hub, stop_event, event_callback=_stm32_event_callback)
        runtimes[route_prefix] = Stm32Runtime(
            route_prefix=route_prefix,
            state=state,
            bridge=bridge,
            sse_hub=sse_hub,
            event_store=event_store,
        )
    return runtimes


def build_room_node_runtimes(
    args: argparse.Namespace,
    stop_event: threading.Event,
    event_store: EventStore | None = None,
) -> dict[str, RoomNodeRuntime]:
    runtimes: dict[str, RoomNodeRuntime] = {}
    for route_prefix, node_id in ROOM_NODE_CONFIGS:
        state = RoomNodeState(node_id=node_id, broker_host=args.mqtt_broker_host, broker_port=args.mqtt_broker_port)
        sse_hub = SerialSseHub()
        runtime = RoomNodeRuntime(
            route_prefix=route_prefix,
            state=state,
            sse_hub=sse_hub,
            stop_event=stop_event,
            broker_host=args.mqtt_broker_host,
            broker_port=args.mqtt_broker_port,
            command_topic=f"smarthome/{node_id}/command",
            sub_topics=[
                f"smarthome/{node_id}/availability",
                f"smarthome/{node_id}/telemetry",
                f"smarthome/{node_id}/state",
            ],
        )
        runtime.event_store = event_store
        runtime.start()
        runtimes[route_prefix] = runtime
    return runtimes


def main() -> int:
    args = parse_args()
    socket.setdefaulttimeout(15)
    stop_event = threading.Event()
    event_store = EventStore(args.event_db_path)

    def _shutdown(signum: int, frame: Any) -> None:
        del signum, frame
        stop_event.set()

    signal.signal(signal.SIGINT, _shutdown)
    signal.signal(signal.SIGTERM, _shutdown)

    tv_state = TvState(
        tv_id="living-room-tv",
        host=args.tv_host,
        port=args.tv_port,
        secure_port=args.tv_secure_port,
    )
    tv_bridge = WebOsTvBridge(state=tv_state, client_key_path=Path(args.tv_client_key_path))
    tv_sse_hub = TvSseHub()
    start_integrated_tv_probe_loop(tv_bridge, tv_state, tv_sse_hub, stop_event, event_store=event_store)
    start_command_worker(tv_bridge, tv_state, tv_sse_hub, stop_event)

    stm32_runtimes = build_stm32_runtimes(args, stop_event, event_store=event_store)
    room_node_runtimes = build_room_node_runtimes(args, stop_event, event_store=event_store)

    Handler.tv_bridge = tv_bridge
    Handler.tv_state = tv_state
    Handler.tv_sse_hub = tv_sse_hub
    Handler.stm32_runtimes = stm32_runtimes
    Handler.room_node_runtimes = room_node_runtimes
    Handler.event_store = event_store

    server = SmartHomeApiServer((args.host, args.port), Handler)
    print(f"smart home api listening on http://{args.host}:{args.port}")
    try:
        while not stop_event.is_set():
            server.handle_request()
    finally:
        server.server_close()
        for runtime in room_node_runtimes.values():
            runtime.stop()
        for runtime in stm32_runtimes.values():
            runtime.bridge.stop()
        event_store.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
