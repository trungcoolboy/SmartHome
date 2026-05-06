#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import queue
import signal
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Any


DEFAULT_MQTT_HOST = "127.0.0.1"
DEFAULT_MQTT_PORT = 1883
MQTT_TOPICS = (
    "smarthome/+/availability",
    "smarthome/+/state",
    "smarthome/+/telemetry",
)


@dataclass(frozen=True)
class NodeTarget:
    node_id: str
    aliases: tuple[str, ...]
    command_topic: str
    kind: str
    channels: tuple[str, ...] = ()
    can_buzz: bool = False
    can_set_led: bool = False
    default_channel: str | None = None


NODE_TARGETS: tuple[NodeTarget, ...] = (
    NodeTarget(
        node_id="living-room-node-01",
        aliases=("living1", "lr1", "living-room-1", "living-room-node-01"),
        command_topic="smarthome/living-room-node-01/command",
        kind="single",
    ),
    NodeTarget(
        node_id="living-room-node-02",
        aliases=("living2", "lr2", "living-room-2", "living-room-node-02"),
        command_topic="smarthome/living-room-node-02/command",
        kind="multi",
        channels=("relay1", "relay2"),
    ),
    NodeTarget(
        node_id="bedroom-2-node-01",
        aliases=("bed1", "b2n1", "bedroom2-1", "bedroom-2-node-01"),
        command_topic="smarthome/bedroom-2-node-01/command",
        kind="single",
    ),
    NodeTarget(
        node_id="bedroom-2-node-02",
        aliases=("bed2", "b2n2", "bedroom2-2", "bedroom-2-node-02"),
        command_topic="smarthome/bedroom-2-node-02/command",
        kind="remote",
        can_set_led=True,
    ),
    NodeTarget(
        node_id="bathroom-1-node-01",
        aliases=("bath1", "bath1-1", "br1", "bathroom1-1", "bathroom-1-node-01"),
        command_topic="smarthome/bathroom-1-node-01/command",
        kind="multi",
        channels=("relay1", "relay2"),
        can_set_led=True,
    ),
    NodeTarget(
        node_id="bathroom-1-node-02",
        aliases=("hotwater", "bath2", "bath1-2", "br2", "bathroom1-2", "bathroom-1-node-02"),
        command_topic="smarthome/bathroom-1-node-02/command",
        kind="single",
        can_buzz=True,
        default_channel=None,
    ),
)

ALIASES = {alias: target for target in NODE_TARGETS for alias in target.aliases}
NODE_BY_ID = {target.node_id: target for target in NODE_TARGETS}
RELAY_COMMANDS = {"on", "off", "toggle", "bat", "tat"}


def compact_json(value: dict[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=True, separators=(",", ":"))


def parse_int_set(value: str) -> set[int]:
    result: set[int] = set()
    for raw_item in value.split(","):
        item = raw_item.strip()
        if not item:
            continue
        try:
            result.add(int(item))
        except ValueError:
            print(f"Ignore invalid chat id: {item}", file=sys.stderr)
    return result


def parse_bool(value: str | None, default: bool = False) -> bool:
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


def decode_payload(payload: str) -> Any:
    try:
        return json.loads(payload)
    except json.JSONDecodeError:
        return payload


def node_id_from_topic(topic: str) -> str | None:
    parts = topic.split("/")
    if len(parts) >= 3 and parts[0] == "smarthome":
        return parts[1]
    return None


def topic_kind(topic: str) -> str:
    parts = topic.split("/")
    if len(parts) >= 3:
        return parts[2]
    return ""


class TelegramClient:
    def __init__(self, token: str, allowed_chat_ids: set[int], notify_chat_id: int | None):
        self.token = token
        self.allowed_chat_ids = allowed_chat_ids
        self.notify_chat_id = notify_chat_id
        self.base_url = f"https://api.telegram.org/bot{token}/"
        self._send_lock = threading.Lock()

    def request(self, method: str, data: dict[str, Any]) -> dict[str, Any]:
        encoded = urllib.parse.urlencode(data).encode("utf-8")
        request = urllib.request.Request(self.base_url + method, data=encoded)
        with urllib.request.urlopen(request, timeout=35) as response:
            body = response.read().decode("utf-8")
        decoded = json.loads(body)
        if not decoded.get("ok"):
            raise RuntimeError(f"Telegram API error: {decoded}")
        return decoded

    def get_updates(self, offset: int | None, timeout: int = 30) -> list[dict[str, Any]]:
        data: dict[str, Any] = {"timeout": timeout, "allowed_updates": json.dumps(["message"])}
        if offset is not None:
            data["offset"] = offset
        return self.request("getUpdates", data).get("result", [])

    def send_message(self, chat_id: int, text: str, disable_notification: bool = False) -> None:
        with self._send_lock:
            self.request(
                "sendMessage",
                {
                    "chat_id": chat_id,
                    "text": text,
                    "disable_notification": "true" if disable_notification else "false",
                },
            )

    def notify(self, text: str) -> None:
        if self.notify_chat_id is None:
            return
        try:
            self.send_message(self.notify_chat_id, text)
        except Exception as exc:  # noqa: BLE001
            print(f"Telegram notify failed: {exc}", file=sys.stderr)

    def authorized(self, chat_id: int) -> bool:
        return not self.allowed_chat_ids or chat_id in self.allowed_chat_ids


class MqttMonitor:
    def __init__(self, host: str, port: int, events: "queue.Queue[tuple[str, str]]"):
        self.host = host
        self.port = port
        self.events = events
        self.stop_event = threading.Event()
        self.process: subprocess.Popen[str] | None = None
        self.thread: threading.Thread | None = None

    def start(self) -> None:
        self.thread = threading.Thread(target=self._run, name="mqtt-monitor", daemon=True)
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

    def _run(self) -> None:
        while not self.stop_event.is_set():
            command = ["mosquitto_sub", "-h", self.host, "-p", str(self.port), "-v"]
            for topic in MQTT_TOPICS:
                command.extend(["-t", topic])
            try:
                self.process = subprocess.Popen(
                    command,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    bufsize=1,
                )
            except FileNotFoundError:
                print("mosquitto_sub not found. Install mosquitto-clients.", file=sys.stderr)
                time.sleep(10)
                continue

            assert self.process.stdout is not None
            for line in self.process.stdout:
                if self.stop_event.is_set():
                    break
                topic, sep, payload = line.rstrip("\n").partition(" ")
                if not sep:
                    continue
                self.events.put((topic, payload))

            if self.process.poll() is None:
                self.process.terminate()
            if not self.stop_event.is_set():
                time.sleep(3)


class TelegramBridge:
    def __init__(self, telegram: TelegramClient, mqtt_host: str, mqtt_port: int, notify_state_events: bool):
        self.telegram = telegram
        self.mqtt_host = mqtt_host
        self.mqtt_port = mqtt_port
        self.notify_state_events = notify_state_events
        self.mqtt_events: "queue.Queue[tuple[str, str]]" = queue.Queue()
        self.mqtt_monitor = MqttMonitor(mqtt_host, mqtt_port, self.mqtt_events)
        self.stop_event = threading.Event()
        self.availability: dict[str, str] = {}
        self.latest_state: dict[str, Any] = {}
        self.latest_telemetry: dict[str, Any] = {}
        self.seen_availability: set[str] = set()

    def run(self) -> None:
        self.mqtt_monitor.start()
        mqtt_thread = threading.Thread(target=self._consume_mqtt_events, name="mqtt-events", daemon=True)
        mqtt_thread.start()
        offset: int | None = None
        print("Telegram bridge started", flush=True)
        while not self.stop_event.is_set():
            try:
                updates = self.telegram.get_updates(offset=offset, timeout=30)
            except (urllib.error.URLError, TimeoutError, RuntimeError, json.JSONDecodeError) as exc:
                print(f"Telegram polling failed: {exc}", file=sys.stderr)
                time.sleep(5)
                continue
            for update in updates:
                offset = int(update["update_id"]) + 1
                self.handle_update(update)

    def stop(self) -> None:
        self.stop_event.set()
        self.mqtt_monitor.stop()

    def _consume_mqtt_events(self) -> None:
        while not self.stop_event.is_set():
            try:
                topic, payload = self.mqtt_events.get(timeout=0.5)
            except queue.Empty:
                continue
            self.handle_mqtt(topic, payload)

    def handle_mqtt(self, topic: str, payload: str) -> None:
        node_id = node_id_from_topic(topic)
        if node_id is None:
            return
        kind = topic_kind(topic)
        decoded = decode_payload(payload)
        if kind == "availability":
            previous = self.availability.get(node_id)
            self.availability[node_id] = str(payload)
            if node_id in self.seen_availability and previous != payload:
                self.telegram.notify(f"{node_id}: {payload}")
            self.seen_availability.add(node_id)
            return
        if kind == "state":
            self.latest_state[node_id] = decoded
            self.notify_state(node_id, decoded)
            return
        if kind == "telemetry":
            self.latest_telemetry[node_id] = decoded

    def notify_state(self, node_id: str, state: Any) -> None:
        if not self.notify_state_events or not isinstance(state, dict):
            return
        event = str(state.get("event", ""))
        interesting = {
            "relay_updated",
            "relay_toggled",
            "touch_toggle",
            "remote_toggle_sent",
            "remote_toggle_failed",
            "buzz",
            "unknown_action",
            "bad_json",
        }
        if event not in interesting:
            return
        self.telegram.notify(self.format_state_line(node_id, state))

    def handle_update(self, update: dict[str, Any]) -> None:
        message = update.get("message") or {}
        chat = message.get("chat") or {}
        chat_id = chat.get("id")
        text = str(message.get("text") or "").strip()
        if not isinstance(chat_id, int) or not text:
            return
        if not self.telegram.authorized(chat_id):
            self.telegram.send_message(chat_id, f"Chat id {chat_id} chua duoc phep.")
            print(f"Unauthorized Telegram chat id: {chat_id}", file=sys.stderr)
            return
        try:
            response = self.handle_command(text, chat_id)
        except Exception as exc:  # noqa: BLE001
            response = f"Loi xu ly lenh: {exc}"
            print(response, file=sys.stderr)
        if response:
            self.telegram.send_message(chat_id, response)

    def handle_command(self, text: str, chat_id: int) -> str:
        parts = text.split()
        command = parts[0].split("@", 1)[0].lstrip("/").lower()
        args = parts[1:]
        if command in {"start", "help"}:
            return self.help_text(chat_id)
        if command == "id":
            return f"Chat id: {chat_id}"
        if command == "nodes":
            return self.nodes_text()
        if command == "status":
            return self.status_text()
        if command in RELAY_COMMANDS:
            return self.handle_relay_command(command, args)
        if command == "led":
            return self.handle_led_command(args)
        if command == "buzz":
            return self.handle_buzz_command(args)
        if command in {"pingnode", "ping-node"}:
            return self.handle_ping_node(args)
        return "Lenh khong ho tro. Go /help de xem lenh."

    def help_text(self, chat_id: int) -> str:
        return (
            "SmartHome Telegram\n"
            f"Chat id: {chat_id}\n\n"
            "Lenh:\n"
            "/status - xem trang thai node\n"
            "/nodes - xem alias node\n"
            "/on <node> [relay1|relay2]\n"
            "/off <node> [relay1|relay2]\n"
            "/toggle <node> [relay1|relay2]\n"
            "/led <node> [channel] red|green|off\n"
            "/buzz hotwater [ms]\n"
            "/pingnode <node>\n\n"
            "Vi du:\n"
            "/on bath1-1 relay1\n"
            "/toggle hotwater\n"
            "/buzz hotwater 120"
        )

    def nodes_text(self) -> str:
        lines = ["Node aliases:"]
        for target in NODE_TARGETS:
            channels = f" channels={','.join(target.channels)}" if target.channels else ""
            lines.append(f"{target.node_id}: {', '.join(target.aliases)}{channels}")
        return "\n".join(lines)

    def status_text(self) -> str:
        lines = ["SmartHome status:"]
        for target in NODE_TARGETS:
            state = self.latest_state.get(target.node_id) or self.latest_telemetry.get(target.node_id)
            avail = self.availability.get(target.node_id, "?")
            lines.append(self.format_state_line(target.node_id, state, avail))
        return "\n".join(lines)

    def handle_relay_command(self, command: str, args: list[str]) -> str:
        if not args:
            return "Thieu node. Vi du: /on bath1-1 relay1"
        target = self.resolve_node(args[0])
        if target is None:
            return f"Khong biet node: {args[0]}"
        if target.kind == "remote":
            if command not in {"toggle"}:
                return f"{target.node_id} chi ho tro /toggle de dieu khien remote relay."
            self.publish(target.command_topic, {"action": "toggle_remote"})
            return f"Da gui toggle remote relay toi {target.node_id}"
        payload: dict[str, Any]
        channel = self.resolve_channel(target, args[1] if len(args) > 1 else None)
        if target.channels and channel is None:
            return f"{target.node_id} can channel: {', '.join(target.channels)}"
        if command in {"on", "bat"}:
            payload = {"action": "set_relay", "value": True}
        elif command in {"off", "tat"}:
            payload = {"action": "set_relay", "value": False}
        else:
            payload = {"action": "toggle_relay"}
        if channel:
            payload["channel"] = channel
        self.publish(target.command_topic, payload)
        detail = f" {channel}" if channel else ""
        return f"Da gui {payload['action']}{detail} toi {target.node_id}"

    def handle_led_command(self, args: list[str]) -> str:
        if len(args) < 2:
            return "Dung: /led <node> [channel] red|green|off"
        target = self.resolve_node(args[0])
        if target is None:
            return f"Khong biet node: {args[0]}"
        if not target.can_set_led:
            return f"{target.node_id} chua ho tro set_led."
        value = args[-1].lower()
        if value not in {"red", "green", "off", "high", "low", "hiz", "hi-z"}:
            return "Mau LED phai la red, green hoac off."
        payload: dict[str, Any] = {"action": "set_led", "value": value}
        if target.channels:
            channel = self.resolve_channel(target, args[1] if len(args) > 2 else None)
            if channel is None:
                return f"{target.node_id} can channel LED: relay1, relay2, touch3"
            payload["channel"] = channel
        self.publish(target.command_topic, payload)
        return f"Da gui LED {value} toi {target.node_id}"

    def handle_buzz_command(self, args: list[str]) -> str:
        if not args:
            return "Dung: /buzz hotwater [ms]"
        target = self.resolve_node(args[0])
        if target is None:
            return f"Khong biet node: {args[0]}"
        if not target.can_buzz:
            return f"{target.node_id} khong co buzzer."
        duration_ms = 120
        if len(args) >= 2:
            try:
                duration_ms = max(20, min(3000, int(args[1])))
            except ValueError:
                return "durationMs phai la so."
        self.publish(target.command_topic, {"action": "buzz", "durationMs": duration_ms})
        return f"Da gui buzzer {duration_ms}ms toi {target.node_id}"

    def handle_ping_node(self, args: list[str]) -> str:
        if not args:
            return "Dung: /pingnode <node>"
        target = self.resolve_node(args[0])
        if target is None:
            return f"Khong biet node: {args[0]}"
        self.publish(target.command_topic, {"action": "ping"})
        return f"Da ping {target.node_id}"

    def resolve_node(self, value: str) -> NodeTarget | None:
        value = value.strip().lower()
        return ALIASES.get(value) or NODE_BY_ID.get(value)

    def resolve_channel(self, target: NodeTarget, value: str | None) -> str | None:
        if not target.channels:
            return None
        if value is None:
            return target.default_channel
        normalized = value.strip().lower()
        if normalized in target.channels:
            return normalized
        if normalized in {"1", "r1"} and "relay1" in target.channels:
            return "relay1"
        if normalized in {"2", "r2"} and "relay2" in target.channels:
            return "relay2"
        if normalized in {"3", "touch3", "remote"} and target.node_id == "bathroom-1-node-01":
            return "touch3"
        return None

    def publish(self, topic: str, payload: dict[str, Any]) -> None:
        encoded = compact_json(payload)
        subprocess.run(
            ["mosquitto_pub", "-h", self.mqtt_host, "-p", str(self.mqtt_port), "-t", topic, "-m", encoded],
            check=True,
            capture_output=True,
            text=True,
        )

    def format_state_line(self, node_id: str, state: Any, availability: str | None = None) -> str:
        prefix = node_id
        if availability is not None:
            prefix = f"{node_id} [{availability}]"
        if not isinstance(state, dict):
            return f"{prefix}: chua co state"
        parts: list[str] = []
        event = state.get("event")
        if event:
            parts.append(f"event={event}")
        if "relay" in state:
            parts.append(f"relay={'on' if state.get('relay') else 'off'}")
        if "remoteRelay" in state:
            parts.append(f"remoteRelay={'on' if state.get('remoteRelay') else 'off'}")
        if "touch" in state:
            parts.append(f"touch={'active' if state.get('touch') else 'idle'}")
        if "led" in state:
            parts.append(f"led={state.get('led')}")
        if "channel" in state:
            parts.append(f"channel={state.get('channel')}")
        if "detail" in state:
            parts.append(f"detail={state.get('detail')}")
        channels = state.get("channels")
        if isinstance(channels, list):
            for item in channels:
                if not isinstance(item, dict):
                    continue
                key = item.get("key")
                if not key:
                    continue
                sub_parts = []
                if "relay" in item:
                    sub_parts.append("on" if item.get("relay") else "off")
                if "touchActive" in item:
                    sub_parts.append("touch" if item.get("touchActive") else "idle")
                if "led" in item:
                    sub_parts.append(str(item.get("led")))
                if sub_parts:
                    parts.append(f"{key}={','.join(sub_parts)}")
        if "wifiRssi" in state:
            parts.append(f"rssi={state.get('wifiRssi')}")
        return f"{prefix}: " + (" ".join(parts) if parts else compact_json(state))


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Telegram bridge for SmartHome MQTT nodes")
    parser.add_argument("--mqtt-host", default=os.environ.get("MQTT_HOST", DEFAULT_MQTT_HOST))
    parser.add_argument("--mqtt-port", type=int, default=int(os.environ.get("MQTT_PORT", DEFAULT_MQTT_PORT)))
    parser.add_argument("--bot-token", default=os.environ.get("TELEGRAM_BOT_TOKEN", ""))
    parser.add_argument("--allowed-chat-ids", default=os.environ.get("TELEGRAM_ALLOWED_CHAT_IDS", ""))
    parser.add_argument("--notify-chat-id", default=os.environ.get("TELEGRAM_NOTIFY_CHAT_ID", ""))
    parser.add_argument(
        "--notify-state-events",
        action=argparse.BooleanOptionalAction,
        default=parse_bool(os.environ.get("TELEGRAM_NOTIFY_STATE_EVENTS"), True),
    )
    return parser


def main() -> int:
    args = build_arg_parser().parse_args()
    if not args.bot_token:
        print("Missing TELEGRAM_BOT_TOKEN", file=sys.stderr)
        return 2
    allowed_chat_ids = parse_int_set(args.allowed_chat_ids)
    notify_chat_id = None
    if args.notify_chat_id.strip():
        notify_chat_id = int(args.notify_chat_id.strip())
    if not allowed_chat_ids:
        print("WARNING: TELEGRAM_ALLOWED_CHAT_IDS is empty; every chat can run commands.", file=sys.stderr)
    telegram = TelegramClient(args.bot_token, allowed_chat_ids, notify_chat_id)
    bridge = TelegramBridge(telegram, args.mqtt_host, args.mqtt_port, args.notify_state_events)

    def stop(_signum: int, _frame: object) -> None:
        bridge.stop()

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    try:
        bridge.run()
    finally:
        bridge.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
