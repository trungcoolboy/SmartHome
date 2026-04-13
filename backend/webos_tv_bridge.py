#!/usr/bin/env python3
import argparse
import base64
import json
import os
import queue
import signal
import socket
import ssl
import sys
import threading
import time
from dataclasses import dataclass, field
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from ipaddress import IPv4Network
from pathlib import Path
from typing import Any
from urllib.parse import urlparse


REGISTER_PERMISSIONS = [
    "LAUNCH",
    "LAUNCH_WEBAPP",
    "APP_TO_APP",
    "CLOSE",
    "TEST_OPEN",
    "TEST_PROTECTED",
    "CONTROL_AUDIO",
    "CONTROL_DISPLAY",
    "CONTROL_INPUT_JOYSTICK",
    "CONTROL_INPUT_MEDIA_RECORDING",
    "CONTROL_INPUT_MEDIA_PLAYBACK",
    "CONTROL_INPUT_TV",
    "CONTROL_POWER",
    "READ_APP_STATUS",
    "READ_CURRENT_CHANNEL",
    "READ_INPUT_DEVICE_LIST",
    "READ_NETWORK_STATE",
    "READ_RUNNING_APPS",
    "READ_TV_CHANNEL_LIST",
    "WRITE_NOTIFICATION_TOAST",
    "READ_POWER_STATE",
    "READ_COUNTRY_INFO",
    "CONTROL_INPUT_TEXT",
    "CONTROL_MOUSE_AND_KEYBOARD",
    "READ_INSTALLED_APPS",
]

SIGNED_PERMISSIONS = [
    "TEST_SECURE",
    "CONTROL_INPUT_TEXT",
    "CONTROL_MOUSE_AND_KEYBOARD",
    "READ_INSTALLED_APPS",
    "READ_LGE_SDX",
    "READ_NOTIFICATIONS",
    "SEARCH",
    "WRITE_SETTINGS",
    "WRITE_NOTIFICATION_ALERT",
    "CONTROL_POWER",
    "READ_CURRENT_CHANNEL",
    "READ_RUNNING_APPS",
    "READ_UPDATE_INFO",
    "UPDATE_FROM_REMOTE_APP",
    "READ_LGE_TV_INPUT_EVENTS",
    "READ_TV_CURRENT_TIME",
]


@dataclass
class TvState:
    tv_id: str
    host: str
    port: int
    secure_port: int
    mac_address: str | None = None
    boot_time: float = field(default_factory=time.time)
    reachable: bool = False
    paired: bool = False
    pairing_pending: bool = False
    client_key_present: bool = False
    last_seen: float | None = None
    volume: int | None = None
    muted: bool | None = None
    foreground_app_id: str | None = None
    inputs: list[dict[str, Any]] = field(default_factory=list)
    last_error: str | None = None
    wake_pending: bool = False
    lock: threading.Lock = field(default_factory=threading.Lock)

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            return {
                "tvId": self.tv_id,
                "host": self.host,
                "port": self.port,
                "securePort": self.secure_port,
                "macAddress": self.mac_address,
                "reachable": self.reachable,
                "paired": self.paired,
                "pairingPending": self.pairing_pending,
                "clientKeyPresent": self.client_key_present,
                "lastSeen": self.last_seen,
                "volume": self.volume,
                "muted": self.muted,
                "foregroundAppId": self.foreground_app_id,
                "inputs": self.inputs,
                "lastError": self.last_error,
                "wakePending": self.wake_pending,
                "uptimeSeconds": round(time.time() - self.boot_time, 3),
            }


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


class RealtimeThreadingHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    block_on_close = False


class WebOsClient:
    def __init__(self, host: str, port: int, secure: bool, timeout: float = 5.0, path: str = "/") -> None:
        self.host = host
        self.port = port
        self.secure = secure
        self.timeout = timeout
        self.path = path
        self.sock: socket.socket | ssl.SSLSocket | None = None

    def __enter__(self) -> "WebOsClient":
        self.connect()
        return self

    def __exit__(self, exc_type: Any, exc: Any, tb: Any) -> None:
        del exc_type, exc, tb
        self.close()

    def connect(self) -> None:
        raw = socket.create_connection((self.host, self.port), timeout=self.timeout)
        if self.secure:
            context = ssl.create_default_context()
            context.check_hostname = False
            context.verify_mode = ssl.CERT_NONE
            self.sock = context.wrap_socket(raw, server_hostname=self.host)
        else:
            self.sock = raw

        key = base64.b64encode(os.urandom(16)).decode("ascii")
        request = (
            f"GET {self.path} HTTP/1.1\r\n"
            f"Host: {self.host}:{self.port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
        )
        self.sock.sendall(request.encode("utf-8"))
        response = self.sock.recv(4096).decode("utf-8", errors="replace")
        if "101 Switching Protocols" not in response:
            raise RuntimeError(f"websocket upgrade failed: {response.strip()}")

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def send_json(self, payload: dict[str, Any]) -> None:
        self.send_text(json.dumps(payload, separators=(",", ":"), ensure_ascii=True))

    def send_text(self, payload: str) -> None:
        if self.sock is None:
            raise RuntimeError("socket is not connected")
        data = payload.encode("utf-8")
        mask = os.urandom(4)
        frame = bytearray([0x81])
        size = len(data)
        if size < 126:
            frame.append(0x80 | size)
        elif size < 65536:
            frame.append(0x80 | 126)
            frame.extend(size.to_bytes(2, "big"))
        else:
            frame.append(0x80 | 127)
            frame.extend(size.to_bytes(8, "big"))
        frame.extend(mask)
        frame.extend(bytes(b ^ mask[i % 4] for i, b in enumerate(data)))
        self.sock.sendall(frame)

    def recv_json(self) -> dict[str, Any]:
        if self.sock is None:
            raise RuntimeError("socket is not connected")
        header = self._recv_exact(2)
        opcode = header[0] & 0x0F
        if opcode == 0x8:
            raise RuntimeError("websocket closed by peer")
        if opcode != 0x1:
            raise RuntimeError(f"unexpected websocket opcode: {opcode}")
        size = header[1] & 0x7F
        if size == 126:
            size = int.from_bytes(self._recv_exact(2), "big")
        elif size == 127:
            size = int.from_bytes(self._recv_exact(8), "big")
        body = self._recv_exact(size)
        return json.loads(body.decode("utf-8"))

    def _recv_exact(self, size: int) -> bytes:
        if self.sock is None:
            raise RuntimeError("socket is not connected")
        data = bytearray()
        while len(data) < size:
            chunk = self.sock.recv(size - len(data))
            if not chunk:
                raise RuntimeError("unexpected EOF from websocket")
            data.extend(chunk)
        return bytes(data)


class WebOsTvBridge:
    def __init__(
        self,
        state: TvState,
        client_key_path: Path,
        app_id: str = "com.smarthome.dashboard.remote",
        vendor_id: str = "local.smarthome",
    ) -> None:
        self.state = state
        self.client_key_path = client_key_path
        self.app_id = app_id
        self.vendor_id = vendor_id
        stored_state = self._load_state()
        self.client_key = self._load_client_key()
        self.io_lock = threading.Lock()
        self.command_queue: queue.Queue[dict[str, Any]] = queue.Queue(maxsize=128)
        self.last_command_at = 0.0
        self.last_refresh_at = 0.0
        if stored_state.get("macAddress"):
            self.state.mac_address = stored_state["macAddress"]
        self.state.client_key_present = bool(self.client_key)

    def probe(self, timeout: float = 1.5) -> dict[str, bool]:
        with self.io_lock:
            result = {"port3000": False, "port3001": False}
            for key, port in (("port3000", self.state.port), ("port3001", self.state.secure_port)):
                try:
                    with socket.create_connection((self.state.host, port), timeout=timeout):
                        result[key] = True
                except OSError:
                    result[key] = False
        with self.state.lock:
            self.state.reachable = result["port3000"] or result["port3001"]
            if self.state.reachable:
                self.state.last_seen = time.time()
        return result

    def pair(self) -> dict[str, Any]:
        with self.io_lock:
            return self._register(client_key=self.client_key)

    def refresh(self) -> dict[str, Any]:
        if not self.client_key:
            raise RuntimeError("TV is not paired yet")
        with self.io_lock:
            volume_payload = self._request("ssap://audio/getVolume")
            app_payload = self._request("ssap://com.webos.applicationManager/getForegroundAppInfo")
            inputs_payload = self._request("ssap://tv/getExternalInputList")

        with self.state.lock:
            self.state.reachable = True
            self.state.paired = True
            self.state.pairing_pending = False
            self.state.wake_pending = False
            self.state.last_seen = time.time()
            self.state.volume = volume_payload.get("volume")
            self.state.muted = volume_payload.get("muted")
            self.state.foreground_app_id = app_payload.get("appId")
            self.state.inputs = inputs_payload.get("devices", [])
            self.state.last_error = None
            self.last_refresh_at = time.time()

        return self.state.snapshot()

    def send_command(self, payload: dict[str, Any]) -> dict[str, Any]:
        command = str(payload.get("command", ""))
        if not command:
            raise RuntimeError("missing command")
        self.last_command_at = time.time()
        try:
            self.command_queue.put_nowait(dict(payload))
        except queue.Full:
            raise RuntimeError("command queue is full")
        return {"queued": True, "command": command}

    def _send_command_locked(self, payload: dict[str, Any]) -> dict[str, Any]:
        command = str(payload.get("command", ""))
        if command == "volume_up":
            return self._request("ssap://audio/volumeUp")
        if command == "volume_down":
            return self._request("ssap://audio/volumeDown")
        if command == "set_volume":
            try:
                volume = int(payload.get("volume"))
            except (TypeError, ValueError):
                raise RuntimeError("missing volume")
            if volume < 0 or volume > 100:
                raise RuntimeError("volume must be between 0 and 100")
            return self._request("ssap://audio/setVolume", {"volume": volume})
        if command == "set_mute":
            return self._request("ssap://audio/setMute", {"mute": bool(payload.get("mute", True))})
        if command == "toggle_mute":
            mute = not bool(self.state.muted)
            return self._request("ssap://audio/setMute", {"mute": mute})
        if command == "launch_app":
            app_id = str(payload.get("appId", ""))
            if not app_id:
                raise RuntimeError("missing appId")
            return self._request("ssap://system.launcher/launch", {"id": app_id})
        if command == "set_input":
            app_id = str(payload.get("appId", ""))
            if not app_id:
                raise RuntimeError("missing appId")
            return self._request("ssap://system.launcher/launch", {"id": app_id})
        if command == "show_input_picker":
            return self._request("ssap://com.webos.surfacemanager/showInputPicker")
        if command == "remote_button":
            button = str(payload.get("button", "")).upper()
            if not button:
                raise RuntimeError("missing button")
            return self._send_remote_button(button)
        if command == "turn_off":
            return self._request("ssap://system/turnOff")
        if command == "turn_on":
            return self._wake_on_lan()
        raise RuntimeError(f"unsupported command: {command}")

    def _register(self, client_key: str | None) -> dict[str, Any]:
        manifest = self._manifest()
        payload = {
            "type": "register",
            "id": "register_0",
            "payload": {
                "pairingType": "PROMPT",
                "manifest": manifest,
            },
        }
        if client_key:
            payload["payload"]["client-key"] = client_key

        for secure in (False, True):
            try:
                with WebOsClient(self.state.host, self.state.secure_port if secure else self.state.port, secure=secure) as client:
                    client.send_json(payload)
                    while True:
                        message = client.recv_json()
                        msg_type = message.get("type")
                        body = message.get("payload", {})
                        if msg_type == "registered":
                            new_key = body.get("client-key")
                            if not new_key:
                                raise RuntimeError("TV registered without client-key")
                            self._save_client_key(new_key)
                            with self.state.lock:
                                self.state.reachable = True
                                self.state.paired = True
                                self.state.pairing_pending = False
                                self.state.wake_pending = False
                                self.state.client_key_present = True
                                self.state.last_seen = time.time()
                                self.state.last_error = None
                            return {"paired": True, "clientKeyStored": True}
                        if msg_type == "response" and body.get("pairingType") == "PROMPT" and body.get("returnValue") is True:
                            with self.state.lock:
                                self.state.reachable = True
                                self.state.paired = False
                                self.state.pairing_pending = True
                                self.state.wake_pending = False
                                self.state.last_seen = time.time()
                                self.state.last_error = "pairing confirmation is pending on TV"
                            return {"paired": False, "pairingPending": True}
            except Exception as exc:
                last_error = str(exc)
                continue
        raise RuntimeError(last_error)

    def _request(self, uri: str, payload: dict[str, Any] | None = None) -> dict[str, Any]:
        if not self.client_key:
            raise RuntimeError("TV is not paired yet")
        self._register(client_key=self.client_key)
        request_id = f"req_{int(time.time() * 1000)}"
        message = {"id": request_id, "type": "request", "uri": uri}
        if payload:
            message["payload"] = payload

        for secure in (False, True):
            try:
                with WebOsClient(self.state.host, self.state.secure_port if secure else self.state.port, secure=secure) as client:
                    client.send_json(
                        {
                            "type": "register",
                            "id": "register_0",
                            "payload": {
                                "client-key": self.client_key,
                                "pairingType": "PROMPT",
                                "manifest": {
                                    "manifestVersion": 1,
                                    "appVersion": "1.0",
                                    "signed": {
                                        "created": "20260411",
                                        "appId": self.app_id,
                                        "vendorId": self.vendor_id,
                                        "localizedAppNames": {"": "Smart Home Dashboard"},
                                        "localizedVendorNames": {"": "Smart Home"},
                                        "permissions": REGISTER_PERMISSIONS,
                                        "serial": "202604110001",
                                    },
                                    "permissions": REGISTER_PERMISSIONS,
                                },
                            },
                        }
                    )
                    while True:
                        registered = client.recv_json()
                        if registered.get("type") == "registered":
                            break
                    client.send_json(message)
                    while True:
                        response = client.recv_json()
                        if response.get("id") != request_id:
                            continue
                        body = response.get("payload", {})
                        if not body.get("returnValue", False):
                            raise RuntimeError(body.get("errorText", "TV command failed"))
                        return body
            except Exception as exc:
                last_error = str(exc)
                continue
        with self.state.lock:
            self.state.last_error = last_error
        raise RuntimeError(last_error)

    def _send_remote_button(self, button: str) -> dict[str, Any]:
        pointer_payload = self._request("ssap://com.webos.service.networkinput/getPointerInputSocket")
        socket_path = pointer_payload.get("socketPath")
        if not socket_path:
            raise RuntimeError("TV did not return a pointer socket")

        message_map = {
            "ENTER": "click\n\n",
            "OK": "click\n\n",
            "HOME": "type:button\nname:HOME\n\n",
            "BACK": "type:button\nname:BACK\n\n",
            "UP": "type:button\nname:UP\n\n",
            "DOWN": "type:button\nname:DOWN\n\n",
            "LEFT": "type:button\nname:LEFT\n\n",
            "RIGHT": "type:button\nname:RIGHT\n\n",
            "MENU": "type:button\nname:MENU\n\n",
            "VOLUMEUP": "type:button\nname:VOLUMEUP\n\n",
            "VOLUMEDOWN": "type:button\nname:VOLUMEDOWN\n\n",
            "MUTE": "type:button\nname:MUTE\n\n",
            "CHANNELUP": "type:button\nname:CHANNELUP\n\n",
            "CHANNELDOWN": "type:button\nname:CHANNELDOWN\n\n",
        }
        message = message_map.get(button)
        if message is None:
            raise RuntimeError(f"unsupported remote button: {button}")

        self._send_pointer_message(socket_path, message)
        return {"ok": True, "button": button}

    def _send_pointer_message(self, socket_path: str, message: str) -> None:
        if socket_path.startswith("ws://"):
            secure = False
            socket_path = socket_path[len("ws://") :]
        elif socket_path.startswith("wss://"):
            secure = True
            socket_path = socket_path[len("wss://") :]
        else:
            raise RuntimeError(f"unexpected pointer socket path: {socket_path}")

        host_part, _, path_part = socket_path.partition("/")
        host, _, port_text = host_part.partition(":")
        port = int(port_text or ("3001" if secure else "3000"))
        path = "/" + path_part

        with WebOsClient(host=host, port=port, secure=secure, path=path) as client:
            client.send_text(message)

    def _manifest(self) -> dict[str, Any]:
        return {
            "manifestVersion": 1,
            "appVersion": "1.0",
            "signed": {
                "created": "20260411",
                "appId": self.app_id,
                "vendorId": self.vendor_id,
                "localizedAppNames": {"": "Smart Home Dashboard"},
                "localizedVendorNames": {"": "Smart Home"},
                "permissions": SIGNED_PERMISSIONS,
                "serial": "202604110001",
            },
            "permissions": REGISTER_PERMISSIONS,
        }

    def _load_client_key(self) -> str | None:
        data = self._load_state()
        return data.get("clientKey")

    def _save_client_key(self, client_key: str) -> None:
        self.client_key = client_key
        data = self._load_state()
        if self.state.mac_address:
            data["macAddress"] = self.state.mac_address
        data["clientKey"] = client_key
        self.client_key_path.parent.mkdir(parents=True, exist_ok=True)
        self.client_key_path.write_text(json.dumps(data, ensure_ascii=True, indent=2), encoding="utf-8")

    def _load_state(self) -> dict[str, Any]:
        if not self.client_key_path.exists():
            return {}
        try:
            return json.loads(self.client_key_path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            return {}

    def _wake_on_lan(self) -> dict[str, Any]:
        mac = (self.state.mac_address or "").replace(":", "").replace("-", "").strip()
        if len(mac) != 12:
            raise RuntimeError("missing valid TV MAC address for Wake-on-LAN")

        packet = bytes.fromhex("FF" * 6 + mac * 16)
        destinations = self._wol_destinations()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            for host, port in destinations:
                for _ in range(3):
                    sock.sendto(packet, (host, port))
                    time.sleep(0.08)

        with self.state.lock:
            self.state.last_error = None
            self.state.wake_pending = True
        return {
            "ok": True,
            "wakeSent": True,
            "macAddress": self.state.mac_address,
            "destinations": [f"{host}:{port}" for host, port in destinations],
        }

    def _wol_destinations(self) -> list[tuple[str, int]]:
        hosts = {"255.255.255.255", self.state.host}
        try:
            network = IPv4Network(f"{self.state.host}/24", strict=False)
            hosts.add(str(network.broadcast_address))
        except ValueError:
            pass

        destinations: list[tuple[str, int]] = []
        for host in sorted(hosts):
            for port in (9, 7):
                destinations.append((host, port))
        return destinations


def start_probe_loop(
    bridge: WebOsTvBridge,
    state: TvState,
    sse_hub: SseHub,
    stop_event: threading.Event,
) -> threading.Thread:
    def _run() -> None:
        last_snapshot = ""
        while not stop_event.is_set():
            try:
                if bridge.io_lock.locked():
                    time.sleep(0.05)
                    continue
                bridge.probe(timeout=0.35)
                now = time.time()
                if (
                    bridge.client_key
                    and state.reachable
                    and bridge.command_queue.empty()
                    and now - bridge.last_command_at >= 1.0
                    and now - bridge.last_refresh_at >= 2.0
                    and not bridge.io_lock.locked()
                ):
                    bridge.refresh()
            except Exception as exc:
                with state.lock:
                    state.last_error = str(exc)
                    state.reachable = False
            snapshot = state.snapshot()
            encoded = json.dumps(snapshot, sort_keys=True, ensure_ascii=True)
            if encoded != last_snapshot:
                sse_hub.publish({"type": "snapshot", "state": snapshot})
                last_snapshot = encoded
            time.sleep(0.5)

    thread = threading.Thread(target=_run, name="webos-probe", daemon=True)
    thread.start()
    return thread


def start_command_worker(bridge: WebOsTvBridge, state: TvState, sse_hub: SseHub, stop_event: threading.Event) -> threading.Thread:
    def _run() -> None:
        refresh_after_commands = {
            "volume_up",
            "volume_down",
            "set_volume",
            "set_mute",
            "toggle_mute",
            "set_input",
            "launch_app",
        }
        while not stop_event.is_set():
            try:
                payload = bridge.command_queue.get(timeout=0.5)
            except queue.Empty:
                continue

            try:
                command = str(payload.get("command", ""))
                with bridge.io_lock:
                    bridge._send_command_locked(payload)
                if bridge.client_key and command in refresh_after_commands:
                    bridge.refresh()
                bridge.last_command_at = time.time()
                sse_hub.publish(
                    {
                        "type": "snapshot",
                        "state": state.snapshot(),
                    }
                )
            except Exception as exc:
                with state.lock:
                    state.last_error = str(exc)
                sse_hub.publish({"type": "snapshot", "state": state.snapshot()})
            finally:
                bridge.command_queue.task_done()

    thread = threading.Thread(target=_run, name="webos-command-worker", daemon=True)
    thread.start()
    return thread


def make_handler(state: TvState, bridge: WebOsTvBridge, sse_hub: SseHub):
    tv_path = "/api/tv/living-room"

    class Handler(BaseHTTPRequestHandler):
        server_version = "WebOSTvBridge/0.1"

        def do_OPTIONS(self) -> None:
            self.send_response(HTTPStatus.NO_CONTENT)
            self._write_common_headers(content_type=None, content_length=None)
            self.end_headers()

        def do_GET(self) -> None:
            parsed = urlparse(self.path)
            if parsed.path == "/health":
                self._json(HTTPStatus.OK, {"ok": True, "state": state.snapshot()})
                return
            if parsed.path == f"{tv_path}/status":
                self._json(HTTPStatus.OK, state.snapshot())
                return
            if parsed.path == f"{tv_path}/events":
                self._sse()
                return
            self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})

        def do_POST(self) -> None:
            parsed = urlparse(self.path)
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length) if length else b"{}"
            try:
                body = json.loads(raw.decode("utf-8"))
            except (json.JSONDecodeError, UnicodeDecodeError):
                self._json(HTTPStatus.BAD_REQUEST, {"error": "expected JSON body"})
                return

            try:
                if parsed.path == f"{tv_path}/pair":
                    result = bridge.pair()
                elif parsed.path == f"{tv_path}/refresh":
                    result = bridge.refresh()
                elif parsed.path == f"{tv_path}/command":
                    result = bridge.send_command(body)
                    snapshot = state.snapshot()
                    sse_hub.publish({"type": "snapshot", "state": snapshot})
                    if isinstance(result, dict):
                        result = {**result, "state": snapshot}
                else:
                    self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})
                    return
            except Exception as exc:
                self._json(HTTPStatus.BAD_GATEWAY, {"error": str(exc), "state": state.snapshot()})
                return

            self._json(HTTPStatus.OK, result)

        def log_message(self, format: str, *args: Any) -> None:
            del format, args
            return

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
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.send_header("Access-Control-Allow-Private-Network", "true")

        def _sse(self) -> None:
            client = sse_hub.subscribe()
            self.send_response(HTTPStatus.OK)
            self._write_common_headers(content_type="text/event-stream", content_length=None)
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            try:
                initial = f"data: {json.dumps({'type': 'snapshot', 'state': state.snapshot()}, ensure_ascii=True)}\n\n"
                self.wfile.write(initial.encode("utf-8"))
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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Ubuntu bridge for LG webOS TV")
    parser.add_argument("--tv-id", default="living-room-tv")
    parser.add_argument("--host", default="192.168.1.52")
    parser.add_argument("--port", type=int, default=3000)
    parser.add_argument("--secure-port", type=int, default=3001)
    parser.add_argument("--listen-host", default="0.0.0.0")
    parser.add_argument("--listen-port", type=int, default=8084)
    parser.add_argument(
        "--client-key-path",
        default=str(Path(__file__).with_name("state") / "webos_living_room_tv.json"),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    state = TvState(tv_id=args.tv_id, host=args.host, port=args.port, secure_port=args.secure_port)
    bridge = WebOsTvBridge(state=state, client_key_path=Path(args.client_key_path))
    sse_hub = SseHub()
    stop_event = threading.Event()

    def _shutdown(signum: int, frame: Any) -> None:
        del signum, frame
        stop_event.set()

    signal.signal(signal.SIGINT, _shutdown)
    signal.signal(signal.SIGTERM, _shutdown)

    start_probe_loop(bridge, state, sse_hub, stop_event)
    start_command_worker(bridge, state, sse_hub, stop_event)
    server = RealtimeThreadingHTTPServer((args.listen_host, args.listen_port), make_handler(state, bridge, sse_hub))
    server.timeout = 0.5

    print(
        f"webos tv bridge listening on http://{args.listen_host}:{args.listen_port} "
        f"for {args.tv_id} at {args.host}:{args.port}/{args.secure_port}"
    )
    try:
        while not stop_event.is_set():
            server.handle_request()
    finally:
        server.server_close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
