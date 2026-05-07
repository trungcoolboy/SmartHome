#!/usr/bin/env python3
import json
import sqlite3
import threading
import time
from pathlib import Path
from typing import Any


class EventStore:
    def __init__(self, db_path: str | Path, retention_days: int | float = 30) -> None:
        self.db_path = str(db_path)
        self.retention_days = float(retention_days)
        self.retention_seconds = max(1.0, self.retention_days * 86400.0)
        self.cleanup_interval_seconds = 3600.0
        self.last_cleanup_at = 0.0
        Path(self.db_path).parent.mkdir(parents=True, exist_ok=True)
        self.lock = threading.Lock()
        self.conn = sqlite3.connect(self.db_path, check_same_thread=False)
        self.conn.row_factory = sqlite3.Row
        self._init_schema()
        self.cleanup_old_events(force=True)

    def _init_schema(self) -> None:
        with self.lock:
            self.conn.executescript(
                """
                PRAGMA journal_mode=WAL;
                CREATE TABLE IF NOT EXISTS events (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    ts REAL NOT NULL,
                    source_type TEXT NOT NULL,
                    source_id TEXT NOT NULL,
                    event_type TEXT NOT NULL,
                    direction TEXT,
                    topic TEXT,
                    payload_text TEXT,
                    payload_json TEXT,
                    state_json TEXT,
                    metadata_json TEXT
                );
                CREATE INDEX IF NOT EXISTS idx_events_ts ON events(ts DESC);
                CREATE INDEX IF NOT EXISTS idx_events_source ON events(source_type, source_id, ts DESC);
                CREATE INDEX IF NOT EXISTS idx_events_type ON events(event_type, ts DESC);
                """
            )
            self.conn.commit()

    def _cleanup_old_events_locked(self) -> int:
        cutoff = time.time() - self.retention_seconds
        cursor = self.conn.execute("DELETE FROM events WHERE ts < ?", (cutoff,))
        deleted = cursor.rowcount if cursor.rowcount is not None else 0
        if deleted:
            self.conn.commit()
            self.conn.execute("PRAGMA wal_checkpoint(TRUNCATE)")
        self.last_cleanup_at = time.monotonic()
        return deleted

    def cleanup_old_events(self, *, force: bool = False) -> int:
        now = time.monotonic()
        with self.lock:
            if not force and now - self.last_cleanup_at < self.cleanup_interval_seconds:
                return 0
            return self._cleanup_old_events_locked()

    def close(self) -> None:
        with self.lock:
            self.conn.close()

    def record(
        self,
        *,
        source_type: str,
        source_id: str,
        event_type: str,
        ts: float | None = None,
        direction: str | None = None,
        topic: str | None = None,
        payload_text: str | None = None,
        payload_json: Any | None = None,
        state: Any | None = None,
        metadata: Any | None = None,
    ) -> None:
        encoded_payload = json.dumps(payload_json, ensure_ascii=True) if payload_json is not None else None
        encoded_state = json.dumps(state, ensure_ascii=True) if state is not None else None
        encoded_metadata = json.dumps(metadata, ensure_ascii=True) if metadata is not None else None
        with self.lock:
            self.conn.execute(
                """
                INSERT INTO events (
                    ts, source_type, source_id, event_type, direction, topic,
                    payload_text, payload_json, state_json, metadata_json
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    ts if ts is not None else time.time(),
                    source_type,
                    source_id,
                    event_type,
                    direction,
                    topic,
                    payload_text,
                    encoded_payload,
                    encoded_state,
                    encoded_metadata,
                ),
            )
            self.conn.commit()
            if time.monotonic() - self.last_cleanup_at >= self.cleanup_interval_seconds:
                self._cleanup_old_events_locked()

    def recent_events(
        self,
        *,
        limit: int = 100,
        source_type: str | None = None,
        source_id: str | None = None,
        event_type: str | None = None,
    ) -> list[dict[str, Any]]:
        sql = """
            SELECT id, ts, source_type, source_id, event_type, direction, topic,
                   payload_text, payload_json, state_json, metadata_json
            FROM events
        """
        clauses: list[str] = []
        params: list[Any] = []
        if source_type:
            clauses.append("source_type = ?")
            params.append(source_type)
        if source_id:
            clauses.append("source_id = ?")
            params.append(source_id)
        if event_type:
            clauses.append("event_type = ?")
            params.append(event_type)
        if clauses:
            sql += " WHERE " + " AND ".join(clauses)
        sql += " ORDER BY ts DESC LIMIT ?"
        params.append(max(1, min(limit, 1000)))

        with self.lock:
            rows = self.conn.execute(sql, params).fetchall()

        items: list[dict[str, Any]] = []
        for row in rows:
            items.append(
                {
                    "id": row["id"],
                    "ts": row["ts"],
                    "sourceType": row["source_type"],
                    "sourceId": row["source_id"],
                    "eventType": row["event_type"],
                    "direction": row["direction"],
                    "topic": row["topic"],
                    "payloadText": row["payload_text"],
                    "payloadJson": json.loads(row["payload_json"]) if row["payload_json"] else None,
                    "state": json.loads(row["state_json"]) if row["state_json"] else None,
                    "metadata": json.loads(row["metadata_json"]) if row["metadata_json"] else None,
                }
            )
        return items

    def export_events(
        self,
        *,
        limit: int = 10000,
        source_type: str | None = None,
        source_id: str | None = None,
        event_type: str | None = None,
    ) -> list[dict[str, Any]]:
        sql = """
            SELECT id, ts, source_type, source_id, event_type, direction, topic,
                   payload_text, payload_json, state_json, metadata_json
            FROM events
        """
        clauses: list[str] = []
        params: list[Any] = []
        if source_type:
            clauses.append("source_type = ?")
            params.append(source_type)
        if source_id:
            clauses.append("source_id = ?")
            params.append(source_id)
        if event_type:
            clauses.append("event_type = ?")
            params.append(event_type)
        if clauses:
            sql += " WHERE " + " AND ".join(clauses)
        sql += " ORDER BY ts DESC LIMIT ?"
        params.append(max(1, min(limit, 200000)))

        with self.lock:
            rows = self.conn.execute(sql, params).fetchall()

        items: list[dict[str, Any]] = []
        for row in reversed(rows):
            items.append(
                {
                    "id": row["id"],
                    "ts": row["ts"],
                    "sourceType": row["source_type"],
                    "sourceId": row["source_id"],
                    "eventType": row["event_type"],
                    "direction": row["direction"],
                    "topic": row["topic"],
                    "payloadText": row["payload_text"],
                    "payloadJson": json.loads(row["payload_json"]) if row["payload_json"] else None,
                    "state": json.loads(row["state_json"]) if row["state_json"] else None,
                    "metadata": json.loads(row["metadata_json"]) if row["metadata_json"] else None,
                }
            )
        return items

    def events_between(
        self,
        *,
        event_type: str,
        start_ts: float,
        end_ts: float,
        limit: int = 200000,
    ) -> list[dict[str, Any]]:
        with self.lock:
            rows = self.conn.execute(
                """
                SELECT id, ts, source_type, source_id, event_type, payload_json
                FROM events
                WHERE event_type = ? AND ts >= ? AND ts <= ?
                ORDER BY ts ASC
                LIMIT ?
                """,
                (event_type, start_ts, end_ts, max(1, min(limit, 200000))),
            ).fetchall()

        return [
            {
                "id": row["id"],
                "ts": row["ts"],
                "sourceType": row["source_type"],
                "sourceId": row["source_id"],
                "eventType": row["event_type"],
                "payloadJson": json.loads(row["payload_json"]) if row["payload_json"] else None,
            }
            for row in rows
        ]

    def events_before(
        self,
        *,
        event_type: str,
        before_ts: float,
        limit: int = 1000,
    ) -> list[dict[str, Any]]:
        with self.lock:
            rows = self.conn.execute(
                """
                SELECT id, ts, source_type, source_id, event_type, payload_json
                FROM events
                WHERE event_type = ? AND ts < ?
                ORDER BY ts DESC
                LIMIT ?
                """,
                (event_type, before_ts, max(1, min(limit, 10000))),
            ).fetchall()

        return [
            {
                "id": row["id"],
                "ts": row["ts"],
                "sourceType": row["source_type"],
                "sourceId": row["source_id"],
                "eventType": row["event_type"],
                "payloadJson": json.loads(row["payload_json"]) if row["payload_json"] else None,
            }
            for row in reversed(rows)
        ]

    def stats(self) -> dict[str, Any]:
        with self.lock:
            id_bounds = self.conn.execute("SELECT MIN(id), MAX(id) FROM events").fetchone()
            first = self.conn.execute(
                """
                SELECT id, ts, source_type, source_id, event_type
                FROM events
                ORDER BY id ASC
                LIMIT 1
                """
            ).fetchone()
            last = self.conn.execute(
                """
                SELECT id, ts, source_type, source_id, event_type
                FROM events
                ORDER BY id DESC
                LIMIT 1
                """
            ).fetchone()

        db_path = Path(self.db_path)
        related_size = 0
        for suffix in ("", "-wal", "-shm"):
            path = Path(f"{self.db_path}{suffix}")
            if path.exists():
                related_size += path.stat().st_size

        return {
            "dbPath": str(db_path),
            "dbSizeBytes": related_size,
            "retentionDays": self.retention_days,
            "firstId": id_bounds[0],
            "lastId": id_bounds[1],
            "approxEventCount": ((id_bounds[1] - id_bounds[0] + 1) if id_bounds[0] and id_bounds[1] else 0),
            "firstEvent": dict(first) if first else None,
            "lastEvent": dict(last) if last else None,
        }
