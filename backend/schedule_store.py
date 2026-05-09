#!/usr/bin/env python3
import json
import re
import sqlite3
import threading
import time
from pathlib import Path
from typing import Any


WEEKDAY_KEYS = {0, 1, 2, 3, 4, 5, 6}


class ScheduleStore:
    def __init__(self, db_path: str | Path) -> None:
        self.db_path = str(db_path)
        Path(self.db_path).parent.mkdir(parents=True, exist_ok=True)
        self.lock = threading.Lock()
        self.conn = sqlite3.connect(self.db_path, check_same_thread=False)
        self.conn.row_factory = sqlite3.Row
        self._init_schema()

    def _init_schema(self) -> None:
        with self.lock:
            self.conn.executescript(
                """
                PRAGMA journal_mode=WAL;
                CREATE TABLE IF NOT EXISTS relay_schedules (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    enabled INTEGER NOT NULL DEFAULT 1,
                    label TEXT NOT NULL,
                    route_prefix TEXT NOT NULL,
                    node_id TEXT NOT NULL,
                    channel TEXT NOT NULL,
                    command_type TEXT NOT NULL DEFAULT 'relay',
                    target_state INTEGER NOT NULL,
                    time_of_day TEXT NOT NULL,
                    days_json TEXT NOT NULL,
                    timezone_offset_minutes INTEGER NOT NULL DEFAULT 0,
                    timezone_name TEXT,
                    last_run_key TEXT,
                    last_run_at REAL,
                    last_error TEXT,
                    created_at REAL NOT NULL,
                    updated_at REAL NOT NULL
                );
                CREATE INDEX IF NOT EXISTS idx_relay_schedules_enabled
                    ON relay_schedules(enabled, time_of_day);
                CREATE INDEX IF NOT EXISTS idx_relay_schedules_target
                    ON relay_schedules(route_prefix, channel, command_type);
                CREATE TABLE IF NOT EXISTS alarm_schedules (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    enabled INTEGER NOT NULL DEFAULT 1,
                    label TEXT NOT NULL,
                    route_prefix TEXT NOT NULL,
                    node_id TEXT NOT NULL,
                    duration_ms INTEGER NOT NULL DEFAULT 30000,
                    light_on INTEGER NOT NULL DEFAULT 0,
                    time_of_day TEXT NOT NULL,
                    days_json TEXT NOT NULL,
                    timezone_offset_minutes INTEGER NOT NULL DEFAULT 0,
                    timezone_name TEXT,
                    last_run_key TEXT,
                    last_run_at REAL,
                    last_error TEXT,
                    created_at REAL NOT NULL,
                    updated_at REAL NOT NULL
                );
                CREATE INDEX IF NOT EXISTS idx_alarm_schedules_enabled
                    ON alarm_schedules(enabled, time_of_day);
                CREATE INDEX IF NOT EXISTS idx_alarm_schedules_target
                    ON alarm_schedules(route_prefix);
                """
            )
            self._ensure_column_locked("alarm_schedules", "light_on", "INTEGER NOT NULL DEFAULT 0")
            self.conn.commit()

    def _ensure_column_locked(self, table: str, column: str, definition: str) -> None:
        rows = self.conn.execute(f"PRAGMA table_info({table})").fetchall()
        if any(row["name"] == column for row in rows):
            return
        self.conn.execute(f"ALTER TABLE {table} ADD COLUMN {column} {definition}")

    def close(self) -> None:
        with self.lock:
            self.conn.close()

    def list_schedules(
        self,
        *,
        route_prefix: str | None = None,
        channel: str | None = None,
        command_type: str | None = None,
    ) -> list[dict[str, Any]]:
        sql = "SELECT * FROM relay_schedules"
        clauses: list[str] = []
        params: list[Any] = []
        if route_prefix:
            clauses.append("route_prefix = ?")
            params.append(route_prefix)
        if channel:
            clauses.append("channel = ?")
            params.append(channel)
        if command_type:
            clauses.append("command_type = ?")
            params.append(command_type)
        if clauses:
            sql += " WHERE " + " AND ".join(clauses)
        sql += " ORDER BY time_of_day ASC, id ASC"
        with self.lock:
            rows = self.conn.execute(sql, params).fetchall()
        return [self._row_to_schedule(row) for row in rows]

    def create_schedule(
        self,
        *,
        label: str,
        route_prefix: str,
        node_id: str,
        channel: str,
        command_type: str,
        target_state: bool,
        time_of_day: str,
        days: list[int],
        timezone_offset_minutes: int,
        timezone_name: str | None = None,
        enabled: bool = True,
    ) -> dict[str, Any]:
        normalized_time = self._normalize_time_of_day(time_of_day)
        normalized_days = self._normalize_days(days)
        now = time.time()
        with self.lock:
            cursor = self.conn.execute(
                """
                INSERT INTO relay_schedules (
                    enabled, label, route_prefix, node_id, channel, command_type,
                    target_state, time_of_day, days_json, timezone_offset_minutes,
                    timezone_name, created_at, updated_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    1 if enabled else 0,
                    label.strip() or channel,
                    route_prefix,
                    node_id,
                    channel,
                    command_type,
                    1 if target_state else 0,
                    normalized_time,
                    json.dumps(normalized_days, ensure_ascii=True),
                    int(timezone_offset_minutes),
                    timezone_name,
                    now,
                    now,
                ),
            )
            self.conn.commit()
            row = self.conn.execute(
                "SELECT * FROM relay_schedules WHERE id = ?",
                (cursor.lastrowid,),
            ).fetchone()
        return self._row_to_schedule(row)

    def set_enabled(self, schedule_id: int, enabled: bool) -> dict[str, Any] | None:
        with self.lock:
            self.conn.execute(
                "UPDATE relay_schedules SET enabled = ?, updated_at = ? WHERE id = ?",
                (1 if enabled else 0, time.time(), schedule_id),
            )
            self.conn.commit()
            row = self.conn.execute(
                "SELECT * FROM relay_schedules WHERE id = ?",
                (schedule_id,),
            ).fetchone()
        return self._row_to_schedule(row) if row is not None else None

    def delete_schedule(self, schedule_id: int) -> bool:
        with self.lock:
            cursor = self.conn.execute("DELETE FROM relay_schedules WHERE id = ?", (schedule_id,))
            self.conn.commit()
        return bool(cursor.rowcount)

    def mark_run(self, schedule_id: int, run_key: str, *, error: str | None = None) -> None:
        now = time.time()
        with self.lock:
            self.conn.execute(
                """
                UPDATE relay_schedules
                SET last_run_key = ?, last_run_at = ?, last_error = ?, updated_at = ?
                WHERE id = ?
                """,
                (run_key, now, error, now, schedule_id),
            )
            self.conn.commit()

    def due_schedules(self, now: float | None = None) -> list[dict[str, Any]]:
        current = time.time() if now is None else now
        with self.lock:
            rows = self.conn.execute(
                "SELECT * FROM relay_schedules WHERE enabled = 1 ORDER BY id ASC"
            ).fetchall()

        due: list[dict[str, Any]] = []
        for row in rows:
            item = self._row_to_schedule(row)
            run_key = self._due_run_key(item, current)
            if run_key is None or item.get("lastRunKey") == run_key:
                continue
            item["runKey"] = run_key
            due.append(item)
        return due

    def list_alarms(self, *, route_prefix: str | None = None) -> list[dict[str, Any]]:
        sql = "SELECT * FROM alarm_schedules"
        params: list[Any] = []
        if route_prefix:
            sql += " WHERE route_prefix = ?"
            params.append(route_prefix)
        sql += " ORDER BY time_of_day ASC, id ASC"
        with self.lock:
            rows = self.conn.execute(sql, params).fetchall()
        return [self._row_to_alarm(row) for row in rows]

    def create_alarm(
        self,
        *,
        label: str,
        route_prefix: str,
        node_id: str,
        duration_ms: int,
        light_on: bool,
        time_of_day: str,
        days: list[int],
        timezone_offset_minutes: int,
        timezone_name: str | None = None,
        enabled: bool = True,
    ) -> dict[str, Any]:
        normalized_time = self._normalize_time_of_day(time_of_day)
        normalized_days = self._normalize_days(days)
        normalized_duration_ms = max(1000, min(600000, int(duration_ms)))
        now = time.time()
        with self.lock:
            cursor = self.conn.execute(
                """
                INSERT INTO alarm_schedules (
                    enabled, label, route_prefix, node_id, duration_ms, light_on, time_of_day,
                    days_json, timezone_offset_minutes, timezone_name, created_at, updated_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    1 if enabled else 0,
                    label.strip() or "Alarm",
                    route_prefix,
                    node_id,
                    normalized_duration_ms,
                    1 if light_on else 0,
                    normalized_time,
                    json.dumps(normalized_days, ensure_ascii=True),
                    int(timezone_offset_minutes),
                    timezone_name,
                    now,
                    now,
                ),
            )
            self.conn.commit()
            row = self.conn.execute(
                "SELECT * FROM alarm_schedules WHERE id = ?",
                (cursor.lastrowid,),
            ).fetchone()
        return self._row_to_alarm(row)

    def set_alarm_enabled(self, alarm_id: int, enabled: bool) -> dict[str, Any] | None:
        with self.lock:
            self.conn.execute(
                "UPDATE alarm_schedules SET enabled = ?, updated_at = ? WHERE id = ?",
                (1 if enabled else 0, time.time(), alarm_id),
            )
            self.conn.commit()
            row = self.conn.execute(
                "SELECT * FROM alarm_schedules WHERE id = ?",
                (alarm_id,),
            ).fetchone()
        return self._row_to_alarm(row) if row is not None else None

    def delete_alarm(self, alarm_id: int) -> bool:
        with self.lock:
            cursor = self.conn.execute("DELETE FROM alarm_schedules WHERE id = ?", (alarm_id,))
            self.conn.commit()
        return bool(cursor.rowcount)

    def mark_alarm_run(self, alarm_id: int, run_key: str, *, error: str | None = None) -> None:
        now = time.time()
        with self.lock:
            self.conn.execute(
                """
                UPDATE alarm_schedules
                SET last_run_key = ?, last_run_at = ?, last_error = ?, updated_at = ?
                WHERE id = ?
                """,
                (run_key, now, error, now, alarm_id),
            )
            self.conn.commit()

    def due_alarms(self, now: float | None = None) -> list[dict[str, Any]]:
        current = time.time() if now is None else now
        with self.lock:
            rows = self.conn.execute(
                "SELECT * FROM alarm_schedules WHERE enabled = 1 ORDER BY id ASC"
            ).fetchall()

        due: list[dict[str, Any]] = []
        for row in rows:
            item = self._row_to_alarm(row)
            run_key = self._due_run_key(item, current)
            if run_key is None or item.get("lastRunKey") == run_key:
                continue
            item["runKey"] = run_key
            due.append(item)
        return due

    @staticmethod
    def _due_run_key(item: dict[str, Any], current: float) -> str | None:
        local_ts = current - (item["timezoneOffsetMinutes"] * 60)
        local_time = time.gmtime(local_ts)
        time_key = f"{local_time.tm_hour:02d}:{local_time.tm_min:02d}"
        if item["timeOfDay"] != time_key:
            return None
        if local_time.tm_wday not in item["days"]:
            return None
        return f"{local_time.tm_year:04d}-{local_time.tm_mon:02d}-{local_time.tm_mday:02d} {time_key}"

    @staticmethod
    def _normalize_time_of_day(value: str) -> str:
        raw = str(value).strip()
        if not re.fullmatch(r"\d{2}:\d{2}", raw):
            raise ValueError("timeOfDay must use HH:MM")
        hour, minute = [int(part) for part in raw.split(":", 1)]
        if hour > 23 or minute > 59:
            raise ValueError("timeOfDay is outside 00:00..23:59")
        return raw

    @staticmethod
    def _normalize_days(days: list[int]) -> list[int]:
        normalized = sorted({int(day) for day in days if int(day) in WEEKDAY_KEYS})
        if not normalized:
            raise ValueError("days must include at least one weekday")
        return normalized

    @staticmethod
    def _row_to_schedule(row: sqlite3.Row) -> dict[str, Any]:
        return {
            "id": row["id"],
            "enabled": bool(row["enabled"]),
            "label": row["label"],
            "routePrefix": row["route_prefix"],
            "nodeId": row["node_id"],
            "channel": row["channel"],
            "commandType": row["command_type"],
            "targetState": bool(row["target_state"]),
            "timeOfDay": row["time_of_day"],
            "days": json.loads(row["days_json"]),
            "timezoneOffsetMinutes": row["timezone_offset_minutes"],
            "timezoneName": row["timezone_name"],
            "lastRunKey": row["last_run_key"],
            "lastRunAt": row["last_run_at"],
            "lastError": row["last_error"],
            "createdAt": row["created_at"],
            "updatedAt": row["updated_at"],
        }

    @staticmethod
    def _row_to_alarm(row: sqlite3.Row) -> dict[str, Any]:
        return {
            "id": row["id"],
            "enabled": bool(row["enabled"]),
            "label": row["label"],
            "routePrefix": row["route_prefix"],
            "nodeId": row["node_id"],
            "durationMs": row["duration_ms"],
            "lightOn": bool(row["light_on"]),
            "timeOfDay": row["time_of_day"],
            "days": json.loads(row["days_json"]),
            "timezoneOffsetMinutes": row["timezone_offset_minutes"],
            "timezoneName": row["timezone_name"],
            "lastRunKey": row["last_run_key"],
            "lastRunAt": row["last_run_at"],
            "lastError": row["last_error"],
            "createdAt": row["created_at"],
            "updatedAt": row["updated_at"],
        }
