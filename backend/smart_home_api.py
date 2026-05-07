#!/usr/bin/env python3
import argparse
import cgi
import csv
import io
import json
import os
import re
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
    ("/api/node/bedroom-2-01", "bedroom-2-node-01"),
    ("/api/node/bedroom-2-02", "bedroom-2-node-02"),
    ("/api/node/bathroom-1-01", "bathroom-1-node-01"),
    ("/api/node/bathroom-1-02", "bathroom-1-node-02"),
]
TV_STALE_AFTER_SECONDS = 20.0


def read_system_uptime_seconds() -> float | None:
    try:
        with open("/proc/uptime", "r", encoding="utf-8") as handle:
            raw = handle.read().strip().split()
        if not raw:
            return None
        return float(raw[0])
    except (OSError, ValueError, IndexError):
        return None


@dataclass
class Stm32Runtime:
    route_prefix: str
    state: BridgeState
    bridge: SerialBridge
    sse_hub: SerialSseHub
    event_store: EventStore | None = None
    cache_lock: threading.Lock = field(default_factory=threading.Lock)
    cached_status: dict[str, Any] | None = None
    cached_status_at: float = 0.0
    cached_logs: dict[int, tuple[float, dict[str, Any]]] = field(default_factory=dict)
    command_guard_lock: threading.Lock = field(default_factory=threading.Lock)
    last_control_command_at: dict[str, float] = field(default_factory=dict)
    last_status_refresh_at: float = 0.0


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
    led_modes: dict[str, str] = field(default_factory=dict)
    led_mode: str | None = None
    remote_relay: bool | None = None
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
                "ledModes": dict(self.led_modes),
                "ledMode": self.led_mode,
                "remoteRelay": self.remote_relay,
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
        self.sse_hub.publish({"type": "tx", "topic": self.command_topic, "payload": encoded, "ts": time.time()})

    def _apply_topic_payload(self, topic: str, payload: str) -> None:
        now = time.time()
        parsed_payload: Any | None = None
        relay_changes: list[dict[str, Any]] = []
        try:
            if topic.endswith("/availability"):
                with self.state.lock:
                    self.state.availability = payload.strip()
                    self.state.connected = payload.strip() == "online"
                    self.state.last_seen = now
                    self.state.last_error = None
                parsed_payload = {"availability": payload.strip()}
            elif topic.endswith("/telemetry") or topic.endswith("/state"):
                data = json.loads(payload)
                parsed_payload = data
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
                    if "remoteRelay" in data:
                        new_value = bool(data.get("remoteRelay"))
                        old_value = self.state.remote_relay
                        self.state.remote_relay = new_value
                        if old_value is not None and old_value != new_value:
                            relay_changes.append({"channel": "remoteRelay", "old": old_value, "new": new_value})
                    if "relay" in data:
                        new_value = bool(data.get("relay"))
                        old_value = self.state.relays.get("relay")
                        self.state.relays["relay"] = new_value
                        if old_value is not None and old_value != new_value:
                            relay_changes.append({"channel": "relay", "old": old_value, "new": new_value})
                    if "touch" in data:
                        self.state.touches["touch"] = bool(data.get("touch"))
                    for item in data.get("relays", []):
                        key = str(item.get("key", ""))
                        if not key:
                            continue
                        new_value = bool(item.get("on"))
                        old_value = self.state.relays.get(key)
                        self.state.relays[key] = new_value
                        if old_value is not None and old_value != new_value:
                            relay_changes.append({"channel": key, "old": old_value, "new": new_value})
                        if "touchActive" in item:
                            self.state.touches[key] = bool(item.get("touchActive"))
                        if "ledMode" in item:
                            self.state.led_modes[key] = str(item.get("ledMode"))
                    for item in data.get("channels", []):
                        key = str(item.get("key", ""))
                        if not key:
                            continue
                        if "relayOn" in item:
                            new_value = bool(item.get("relayOn"))
                            old_value = self.state.relays.get(key)
                            self.state.relays[key] = new_value
                            if old_value is not None and old_value != new_value:
                                relay_changes.append({"channel": key, "old": old_value, "new": new_value})
                        if "touchActive" in item:
                            self.state.touches[key] = bool(item.get("touchActive"))
                        if "ledMode" in item:
                            self.state.led_modes[key] = str(item.get("ledMode"))
        except Exception as exc:
            with self.state.lock:
                self.state.last_error = str(exc)
        self.state.append_log("rx", topic, payload)
        if self.event_store is not None and relay_changes:
            self.event_store.record(
                source_type="room_node",
                source_id=self.state.node_id,
                event_type="relay_change",
                direction="rx",
                topic=topic,
                payload_json={"changes": relay_changes},
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


def build_tv_apps_snapshot(state: TvState) -> dict[str, Any]:
    snapshot = state.snapshot()
    return {
        "tvId": snapshot.get("tvId"),
        "host": snapshot.get("host"),
        "reachable": snapshot.get("reachable"),
        "paired": snapshot.get("paired"),
        "foregroundAppId": snapshot.get("foregroundAppId"),
        "launchPoints": snapshot.get("launchPoints", []),
        "apps": snapshot.get("apps", []),
        "appsLastSeen": snapshot.get("appsLastSeen"),
        "appsLastError": snapshot.get("appsLastError"),
        "lastError": snapshot.get("lastError"),
    }


def build_tv_app_history(event_store: EventStore, state: TvState, limit: int = 20) -> dict[str, Any]:
    items = event_store.recent_events(
        limit=limit,
        source_type="tv",
        source_id=state.tv_id,
        event_type="app_session",
    )
    return {"tvId": state.tv_id, "items": items}


def start_temperature_sample_logger(
    state: BridgeState,
    event_store: EventStore,
    stop_event: threading.Event,
    interval_seconds: float = 1.0,
) -> threading.Thread:
    def _run() -> None:
        next_sample_at = time.monotonic()
        while not stop_event.is_set():
            now = time.monotonic()
            if now < next_sample_at:
                stop_event.wait(min(0.2, next_sample_at - now))
                continue
            next_sample_at = now + interval_seconds

            snapshot = state.snapshot()
            temperatures = snapshot.get("temperatures")
            last_seen = snapshot.get("lastSeen")
            if (
                not snapshot.get("connected")
                or not isinstance(temperatures, dict)
                or not temperatures
                or not last_seen
                or time.time() - float(last_seen) > 5.0
            ):
                continue

            event_store.record(
                source_type="stm32",
                source_id=state.board_id,
                event_type="temperature_sample",
                direction="rx",
                payload_json={
                    "readings": [
                        {
                            "sensor": sensor_name,
                            "temp": sensor.get("celsius"),
                            "raw": sensor.get("raw"),
                        }
                        for sensor_name, sensor in temperatures.items()
                        if isinstance(sensor, dict)
                    ],
                },
            )

    thread = threading.Thread(target=_run, name=f"{state.board_id}-temperature-logger", daemon=True)
    thread.start()
    return thread


def start_integrated_tv_probe_loop(
    bridge: WebOsTvBridge,
    state: TvState,
    sse_hub: TvSseHub,
    stop_event: threading.Event,
    event_store: EventStore | None = None,
) -> threading.Thread:
    def _stable_snapshot(snapshot: dict[str, Any]) -> dict[str, Any]:
        return {key: value for key, value in snapshot.items() if key != "uptimeSeconds"}

    def _record_app_transition(previous_snapshot: dict[str, Any], current_snapshot: dict[str, Any], now: float) -> None:
        if event_store is None:
            return
        previous_id = previous_snapshot.get("foregroundAppId")
        current_id = current_snapshot.get("foregroundAppId")
        previous_started_at = previous_snapshot.get("foregroundAppStartedAt")
        previous_title = previous_snapshot.get("foregroundAppTitle") or previous_id
        previous_reachable = bool(previous_snapshot.get("reachable"))
        current_reachable = bool(current_snapshot.get("reachable"))

        if previous_id and previous_started_at and (
            previous_id != current_id or (previous_reachable and not current_reachable)
        ):
            event_store.record(
                source_type="tv",
                source_id=state.tv_id,
                event_type="app_session",
                payload_json={
                    "app": previous_title,
                    "startedAt": previous_started_at,
                    "endedAt": now,
                },
            )

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
                        previous_snapshot = build_tv_snapshot(state)
                        bridge.refresh()
                        current_snapshot = build_tv_snapshot(state)
                        _record_app_transition(previous_snapshot, current_snapshot, now)
                    except Exception as exc:
                        previous_snapshot = build_tv_snapshot(state)
                        with state.lock:
                            state.last_error = str(exc)
                            # Keep cached fields for a grace period, but do not pretend the TV is still reachable.
                            state.reachable = False
                            if not state.last_seen or now - state.last_seen > TV_STALE_AFTER_SECONDS:
                                state.wake_pending = False
                        current_snapshot = build_tv_snapshot(state)
                        _record_app_transition(previous_snapshot, current_snapshot, now)
                elif not reachable:
                    previous_snapshot = build_tv_snapshot(state)
                    with state.lock:
                        state.reachable = False
                        if not state.last_seen or now - state.last_seen > TV_STALE_AFTER_SECONDS:
                            state.wake_pending = False
                    current_snapshot = build_tv_snapshot(state)
                    _record_app_transition(previous_snapshot, current_snapshot, now)
            except Exception as exc:
                now = time.time()
                previous_snapshot = build_tv_snapshot(state)
                with state.lock:
                    state.last_error = str(exc)
                    state.reachable = False
                    if not state.last_seen or now - state.last_seen > TV_STALE_AFTER_SECONDS:
                        state.wake_pending = False
                current_snapshot = build_tv_snapshot(state)
                _record_app_transition(previous_snapshot, current_snapshot, now)

            snapshot = build_tv_snapshot(state)
            encoded = json.dumps(_stable_snapshot(snapshot), sort_keys=True, ensure_ascii=True)
            if encoded != last_snapshot:
                sse_hub.publish({"type": "snapshot", "state": snapshot})
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
    upload_dir: Path = Path("/tmp")
    stm32_status_cache_ttl_seconds = 0.5
    stm32_logs_cache_ttl_seconds = 0.75

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
        if parsed.path == "/api/history/stats":
            self._history_stats()
            return
        if parsed.path == "/api/history/export":
            self._history_export()
            return
        if parsed.path == "/api/history":
            self._history()
            return
        if parsed.path == "/api/uploads":
            self._uploads()
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
        return {
            "ok": True,
            "serverTime": time.time(),
            "serverUptimeSeconds": read_system_uptime_seconds(),
            "services": services,
        }

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

    def _history_stats(self) -> None:
        self._json(HTTPStatus.OK, self._require_event_store().stats())

    def _temperature_export_rows(self, items: list[dict[str, Any]]) -> list[dict[str, Any]]:
        rows: list[dict[str, Any]] = []
        for item in items:
            payload = item.get("payloadJson")
            if not isinstance(payload, dict):
                continue
            readings = payload.get("readings")
            if isinstance(readings, list):
                for reading in readings:
                    if not isinstance(reading, dict):
                        continue
                    rows.append(
                        {
                            "time": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(float(item["ts"]))),
                            "sensor": reading.get("sensor"),
                            "temp": reading.get("temp"),
                            "raw": reading.get("raw"),
                        }
                    )
                continue
            temperatures = payload.get("temperatures")
            if isinstance(temperatures, dict):
                for sensor_name, sensor in temperatures.items():
                    if not isinstance(sensor, dict):
                        continue
                    rows.append(
                        {
                            "time": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(float(item["ts"]))),
                            "sensor": sensor_name,
                            "temp": sensor.get("celsius"),
                            "raw": sensor.get("raw"),
                        }
                    )
        for index, row in enumerate(rows, start=1):
            row["stt"] = index
        return rows

    def _relay_export_rows(self, items: list[dict[str, Any]]) -> list[dict[str, Any]]:
        rows: list[dict[str, Any]] = []
        for item in items:
            payload = item.get("payloadJson")
            if not isinstance(payload, dict):
                continue
            source = item.get("sourceId")
            event_type = item.get("eventType")
            row_time = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(float(item["ts"])))
            if event_type in {"relay_change", "control_change"}:
                changes = payload.get("changes")
                if not isinstance(changes, list):
                    continue
                for change in changes:
                    if not isinstance(change, dict):
                        continue
                    rows.append(
                        {
                            "time": row_time,
                            "source": source,
                            "relay": change.get("channel") or change.get("controlId"),
                            "state": change.get("new"),
                        }
                    )
                continue
            if event_type == "relay_command":
                rows.append(
                    {
                        "time": row_time,
                        "source": source,
                        "relay": payload.get("channel") or "relay",
                        "state": payload.get("value") if "value" in payload else payload.get("action"),
                    }
                )
                continue
            if event_type == "control_command":
                rows.append(
                    {
                        "time": row_time,
                        "source": source,
                        "relay": payload.get("controlId"),
                        "state": payload.get("value"),
                    }
                )
        for index, row in enumerate(rows, start=1):
            row["stt"] = index
        return rows

    def _tv_app_export_rows(self, items: list[dict[str, Any]]) -> list[dict[str, Any]]:
        rows: list[dict[str, Any]] = []
        for item in items:
            payload = item.get("payloadJson")
            if not isinstance(payload, dict):
                continue
            started_at = payload.get("startedAt")
            ended_at = payload.get("endedAt")
            rows.append(
                {
                    "app": payload.get("app") or payload.get("title") or payload.get("appId"),
                    "startedAt": (
                        time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(float(started_at)))
                        if started_at is not None
                        else ""
                    ),
                    "endedAt": (
                        time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(float(ended_at)))
                        if ended_at is not None
                        else ""
                    ),
                }
            )
        for index, row in enumerate(rows, start=1):
            row["stt"] = index
        return rows

    def _history_export(self) -> None:
        params = parse_qs(urlparse(self.path).query)
        limit = max(1, min(int(params.get("limit", ["86400"])[0]), 200000))
        source_type = params.get("source_type", [None])[0]
        source_id = params.get("source_id", [None])[0]
        event_type = params.get("event_type", [None])[0]
        export_format = params.get("format", ["csv"])[0].lower()
        items = self._require_event_store().export_events(
            limit=limit,
            source_type=source_type,
            source_id=source_id,
            event_type=event_type,
        )

        label = event_type or "all"
        timestamp = time.strftime("%Y%m%d-%H%M%S", time.gmtime())
        if event_type == "temperature_sample":
            rows = self._temperature_export_rows(items)
            if export_format == "json":
                body = json.dumps({"items": rows}, ensure_ascii=True, indent=2).encode("utf-8")
                self._download_response(
                    body,
                    content_type="application/json; charset=utf-8",
                    filename=f"smart-home-{label}-{timestamp}.json",
                )
                return
            output = io.StringIO()
            writer = csv.DictWriter(output, fieldnames=["stt", "time", "sensor", "temp", "raw"])
            writer.writeheader()
            writer.writerows(rows)
            self._download_response(
                output.getvalue().encode("utf-8"),
                content_type="text/csv; charset=utf-8",
                filename=f"smart-home-{label}-{timestamp}.csv",
            )
            return

        if event_type in {"relay_change", "relay_command", "control_change", "control_command"}:
            rows = self._relay_export_rows(items)
            if export_format == "json":
                body = json.dumps({"items": rows}, ensure_ascii=True, indent=2).encode("utf-8")
                self._download_response(
                    body,
                    content_type="application/json; charset=utf-8",
                    filename=f"smart-home-{label}-{timestamp}.json",
                )
                return
            output = io.StringIO()
            writer = csv.DictWriter(output, fieldnames=["stt", "time", "source", "relay", "state"])
            writer.writeheader()
            writer.writerows(rows)
            self._download_response(
                output.getvalue().encode("utf-8"),
                content_type="text/csv; charset=utf-8",
                filename=f"smart-home-{label}-{timestamp}.csv",
            )
            return

        if event_type == "app_session":
            rows = self._tv_app_export_rows(items)
            if export_format == "json":
                body = json.dumps({"items": rows}, ensure_ascii=True, indent=2).encode("utf-8")
                self._download_response(
                    body,
                    content_type="application/json; charset=utf-8",
                    filename=f"smart-home-{label}-{timestamp}.json",
                )
                return
            output = io.StringIO()
            writer = csv.DictWriter(output, fieldnames=["stt", "app", "startedAt", "endedAt"])
            writer.writeheader()
            writer.writerows(rows)
            self._download_response(
                output.getvalue().encode("utf-8"),
                content_type="text/csv; charset=utf-8",
                filename=f"smart-home-{label}-{timestamp}.csv",
            )
            return

        if export_format == "json":
            body = json.dumps({"items": items}, ensure_ascii=True, indent=2).encode("utf-8")
            self._download_response(
                body,
                content_type="application/json; charset=utf-8",
                filename=f"smart-home-{label}-{timestamp}.json",
            )
            return

        output = io.StringIO()
        writer = csv.DictWriter(
            output,
            fieldnames=[
                "id",
                "isoTime",
                "unixTime",
                "sourceType",
                "sourceId",
                "eventType",
                "direction",
                "topic",
                "payloadText",
                "payloadJson",
                "state",
                "metadata",
            ],
        )
        writer.writeheader()
        for item in items:
            writer.writerow(
                {
                    "id": item["id"],
                    "isoTime": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(float(item["ts"]))),
                    "unixTime": item["ts"],
                    "sourceType": item["sourceType"],
                    "sourceId": item["sourceId"],
                    "eventType": item["eventType"],
                    "direction": item["direction"],
                    "topic": item["topic"],
                    "payloadText": item["payloadText"],
                    "payloadJson": json.dumps(item["payloadJson"], ensure_ascii=True) if item["payloadJson"] is not None else "",
                    "state": json.dumps(item["state"], ensure_ascii=True) if item["state"] is not None else "",
                    "metadata": json.dumps(item["metadata"], ensure_ascii=True) if item["metadata"] is not None else "",
                }
            )
        self._download_response(
            output.getvalue().encode("utf-8"),
            content_type="text/csv; charset=utf-8",
            filename=f"smart-home-{label}-{timestamp}.csv",
        )

    def _download_response(self, body: bytes, *, content_type: str, filename: str) -> None:
        self.send_response(HTTPStatus.OK)
        self._write_common_headers(content_type=content_type, content_length=len(body))
        self.send_header("Content-Disposition", f'attachment; filename="{filename}"')
        self.end_headers()
        self.wfile.write(body)

    def _uploads(self) -> None:
        if self.command == "GET":
            self._json(HTTPStatus.OK, {"items": self._list_uploads()})
            return
        if self.command == "POST":
            self._upload_file()
            return
        self._json(HTTPStatus.METHOD_NOT_ALLOWED, {"error": "method not allowed"})

    def _list_uploads(self) -> list[dict[str, Any]]:
        upload_dir = self.upload_dir
        upload_dir.mkdir(parents=True, exist_ok=True)
        items: list[dict[str, Any]] = []
        for path in sorted(upload_dir.iterdir(), key=lambda item: item.stat().st_mtime, reverse=True):
            if not path.is_file():
                continue
            stat = path.stat()
            items.append(
                {
                    "name": path.name,
                    "path": str(path),
                    "size": stat.st_size,
                    "modifiedAt": stat.st_mtime,
                }
            )
        return items

    def _upload_file(self) -> None:
        content_type = self.headers.get("Content-Type", "")
        if "multipart/form-data" not in content_type:
            self._json(HTTPStatus.BAD_REQUEST, {"error": "expected multipart/form-data"})
            return

        try:
            form = cgi.FieldStorage(
                fp=self.rfile,
                headers=self.headers,
                environ={
                    "REQUEST_METHOD": "POST",
                    "CONTENT_TYPE": content_type,
                    "CONTENT_LENGTH": self.headers.get("Content-Length", "0"),
                },
                keep_blank_values=True,
            )
        except Exception as exc:
            self._json(HTTPStatus.BAD_REQUEST, {"error": f"invalid multipart body: {exc}"})
            return

        file_field = form["file"] if "file" in form else None
        if file_field is None:
            self._json(HTTPStatus.BAD_REQUEST, {"error": "missing file field"})
            return
        if isinstance(file_field, list):
            file_field = file_field[0]
        filename = Path(str(getattr(file_field, "filename", "") or "")).name
        if not filename:
            self._json(HTTPStatus.BAD_REQUEST, {"error": "missing filename"})
            return
        if getattr(file_field, "file", None) is None:
            self._json(HTTPStatus.BAD_REQUEST, {"error": "empty file payload"})
            return

        safe_name = self._sanitize_upload_name(filename)
        upload_dir = self.upload_dir
        upload_dir.mkdir(parents=True, exist_ok=True)
        destination = self._allocate_upload_path(upload_dir / safe_name)

        try:
            data = file_field.file.read()
            destination.write_bytes(data)
        except Exception as exc:
            self._json(HTTPStatus.INTERNAL_SERVER_ERROR, {"error": f"failed to save file: {exc}"})
            return

        self._json(
            HTTPStatus.OK,
            {
                "ok": True,
                "item": {
                    "name": destination.name,
                    "path": str(destination),
                    "size": destination.stat().st_size,
                    "modifiedAt": destination.stat().st_mtime,
                },
                "items": self._list_uploads(),
            },
        )

    def _sanitize_upload_name(self, filename: str) -> str:
        cleaned = re.sub(r"[^A-Za-z0-9._-]+", "-", filename).strip(".-")
        return cleaned or f"upload-{int(time.time())}"

    def _allocate_upload_path(self, target: Path) -> Path:
        if not target.exists():
            return target
        stem = target.stem
        suffix = target.suffix
        index = 2
        while True:
            candidate = target.with_name(f"{stem}-{index}{suffix}")
            if not candidate.exists():
                return candidate
            index += 1

    def _handle_tv_request(self, parsed: Any) -> None:
        if parsed.path == f"{TV_ROUTE_PREFIX}/status":
            self._json(HTTPStatus.OK, build_tv_snapshot(self._require_tv_state()))
            return
        if parsed.path == f"{TV_ROUTE_PREFIX}/apps":
            if self.command == "GET":
                self._json(HTTPStatus.OK, build_tv_apps_snapshot(self._require_tv_state()))
                return
            self._tv_action("apps")
            return
        if parsed.path == f"{TV_ROUTE_PREFIX}/app-history":
            params = parse_qs(parsed.query)
            limit = max(1, min(int(params.get("limit", ["20"])[0]), 100))
            self._json(HTTPStatus.OK, build_tv_app_history(self._require_event_store(), self._require_tv_state(), limit))
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
            elif action == "apps":
                result = bridge.list_apps()
            elif action == "command":
                result = bridge.send_command(body)
            else:
                self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})
                return
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
            now = time.time()
            with runtime.cache_lock:
                if (
                    runtime.cached_status is None
                    or now - runtime.cached_status_at >= self.stm32_status_cache_ttl_seconds
                ):
                    runtime.cached_status = runtime.state.snapshot()
                    runtime.cached_status_at = now
                payload = runtime.cached_status
            self._json(HTTPStatus.OK, payload)
            return
        if parsed.path == f"{runtime.route_prefix}/logs":
            params = parse_qs(parsed.query)
            limit = max(1, min(int(params.get("limit", ["50"])[0]), 200))
            now = time.time()
            with runtime.cache_lock:
                cached_entry = runtime.cached_logs.get(limit)
                if cached_entry is None or now - cached_entry[0] >= self.stm32_logs_cache_ttl_seconds:
                    payload = {"items": runtime.state.recent_log(limit)}
                    runtime.cached_logs[limit] = (now, payload)
                    if len(runtime.cached_logs) > 8:
                        runtime.cached_logs.clear()
                else:
                    payload = cached_entry[1]
            self._json(HTTPStatus.OK, payload)
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
        normalized_text = " ".join(text.strip().split()).lower()
        if normalized_text == "status":
            self._json(HTTPStatus.OK, {"ok": True, "sent": text, "deferred": True, "state": runtime.state.snapshot()})
            return
        command_parts = normalized_text.split()
        if len(command_parts) >= 3 and command_parts[0] in {"pump", "misc"}:
            control_id = f"{command_parts[0]}.{command_parts[1]}"
            control = runtime.state.snapshot().get("controls", {}).get(control_id)
            if command_parts[2] == "mode" and len(command_parts) >= 4 and control and control.get("mode") == command_parts[3]:
                self._json(HTTPStatus.OK, {"ok": True, "sent": text, "deferred": True, "state": runtime.state.snapshot()})
                return
            if command_parts[2] in {"on", "off"} and control and control.get("state") == command_parts[2]:
                self._json(HTTPStatus.OK, {"ok": True, "sent": text, "deferred": True, "state": runtime.state.snapshot()})
                return
            with runtime.command_guard_lock:
                runtime.last_control_command_at[control_id] = time.monotonic()
        try:
            runtime.bridge.send_text(text)
        except Exception as exc:
            self._json(HTTPStatus.BAD_GATEWAY, {"error": str(exc)})
            return
        runtime.sse_hub.publish({"type": "tx", "payload": text, "ts": time.time()})
        self._json(HTTPStatus.OK, {"ok": True, "sent": text, "state": runtime.state.snapshot()})

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
    parser.add_argument(
        "--upload-dir",
        default=str(Path("/home/trungcoolboy/smart-home/uploads/dashboard")),
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
        if event_store is not None:
            start_temperature_sample_logger(state, event_store, stop_event)
        last_control_states_ref: dict[str, dict[str, str]] = {"states": {}}

        def _control_states(snapshot: dict[str, Any]) -> dict[str, str]:
            controls = snapshot.get("controls")
            if not isinstance(controls, dict):
                return {}
            states: dict[str, str] = {}
            for control_id, control in controls.items():
                if not isinstance(control, dict):
                    continue
                value = control.get("state")
                if value in {"on", "off"}:
                    states[str(control_id)] = str(value)
            return states

        def _stm32_event_callback(
            event: dict[str, Any],
            snapshot: dict[str, Any],
            board_id: str = board_id,
            last_control_states_ref: dict[str, dict[str, str]] = last_control_states_ref,
        ) -> None:
            if event_store is None:
                return
            if event["type"] != "snapshot":
                return
            current_control_states = _control_states(snapshot)
            previous_control_states = last_control_states_ref["states"]
            if not previous_control_states:
                last_control_states_ref["states"] = current_control_states
                return
            changes = [
                {"controlId": control_id, "old": previous_control_states[control_id], "new": value}
                for control_id, value in current_control_states.items()
                if control_id in previous_control_states and previous_control_states[control_id] != value
            ]
            last_control_states_ref["states"] = current_control_states
            if not changes:
                return
            event_store.record(
                source_type="stm32",
                source_id=board_id,
                event_type="control_change",
                direction="rx",
                payload_json={"changes": changes},
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
    Handler.upload_dir = Path(args.upload_dir)

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
