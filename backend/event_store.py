#!/usr/bin/env python3
import json
import sqlite3
import threading
import time
from pathlib import Path
from typing import Any


class EventStore:
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
