import React, { useEffect, useMemo, useState } from "react";
import { getApiBaseUrl } from "../api";

const eventTypeOptions = [
  { value: "", label: "All logs" },
  { value: "temperature_sample", label: "Temperature 1s" },
  { value: "relay_change", label: "Relay changed" },
  { value: "relay_command", label: "Relay command" },
  { value: "control_change", label: "STM32 control changed" },
  { value: "control_command", label: "STM32 control command" },
  { value: "app_session", label: "TV app sessions" },
];

const sourceOptions = [
  { value: "", label: "All sources" },
  { value: "stm32", label: "STM32" },
  { value: "room_node", label: "Room nodes" },
  { value: "tv", label: "TV" },
];

function formatDateTime(unixSeconds) {
  if (!Number.isFinite(Number(unixSeconds))) {
    return "-";
  }
  return new Date(Number(unixSeconds) * 1000).toLocaleString();
}

function formatPayload(item) {
  const payload = item.payloadJson;
  if (!payload) {
    return item.payloadText || "-";
  }
  if (item.eventType === "temperature_sample" && Array.isArray(payload.readings)) {
    return payload.readings
      .map((sensor) => `${sensor.sensor}: ${sensor.temp}C raw ${sensor.raw}`)
      .join(" / ");
  }
  if (item.eventType === "temperature_sample" && payload.temperatures) {
    return Object.values(payload.temperatures)
      .map((sensor) => `${sensor.key}: ${sensor.celsius}C`)
      .join(" / ");
  }
  if (Array.isArray(payload.changes)) {
    return payload.changes
      .map((change) => `${change.channel || change.controlId}: ${change.old} -> ${change.new}`)
      .join(" / ");
  }
  return JSON.stringify(payload);
}

function buildTemperatureRows(items) {
  const rows = [];
  for (const item of items) {
    const payload = item.payloadJson;
    if (!payload) {
      continue;
    }
    if (Array.isArray(payload.readings)) {
      for (const reading of payload.readings) {
        rows.push({
          id: `${item.id}-${reading.sensor}`,
          time: item.ts,
          sensor: reading.sensor,
          temp: reading.temp,
          raw: reading.raw,
        });
      }
      continue;
    }
    if (payload.temperatures) {
      for (const [sensor, reading] of Object.entries(payload.temperatures)) {
        rows.push({
          id: `${item.id}-${sensor}`,
          time: item.ts,
          sensor,
          temp: reading.celsius,
          raw: reading.raw,
        });
      }
    }
  }
  return rows.map((row, index) => ({ ...row, stt: index + 1 }));
}

function buildRelayRows(items) {
  const rows = [];
  for (const item of items) {
    const payload = item.payloadJson;
    if (!payload) {
      continue;
    }
    if ((item.eventType === "relay_change" || item.eventType === "control_change") && Array.isArray(payload.changes)) {
      for (const change of payload.changes) {
        rows.push({
          id: `${item.id}-${change.channel || change.controlId}`,
          time: item.ts,
          source: item.sourceId,
          relay: change.channel || change.controlId,
          state: change.new,
        });
      }
      continue;
    }
    if (item.eventType === "relay_command") {
      rows.push({
        id: String(item.id),
        time: item.ts,
        source: item.sourceId,
        relay: payload.channel || "relay",
        state: Object.prototype.hasOwnProperty.call(payload, "value") ? payload.value : payload.action,
      });
      continue;
    }
    if (item.eventType === "control_command") {
      rows.push({
        id: String(item.id),
        time: item.ts,
        source: item.sourceId,
        relay: payload.controlId,
        state: payload.value,
      });
    }
  }
  return rows.map((row, index) => ({ ...row, stt: index + 1 }));
}

function buildTvRows(items) {
  const rows = [];
  for (const item of items) {
    const payload = item.payloadJson;
    if (!payload) {
      continue;
    }
    rows.push({
      id: String(item.id),
      app: payload.app || payload.title || payload.appId,
      startedAt: payload.startedAt,
      endedAt: payload.endedAt,
    });
  }
  return rows.map((row, index) => ({ ...row, stt: index + 1 }));
}

function isRelayEventType(eventType) {
  return ["relay_change", "relay_command", "control_change", "control_command"].includes(eventType);
}

function buildQuery({ eventType, sourceType, limit, format }) {
  const params = new URLSearchParams();
  if (eventType) {
    params.set("event_type", eventType);
  }
  if (sourceType) {
    params.set("source_type", sourceType);
  }
  if (limit) {
    params.set("limit", String(limit));
  }
  if (format) {
    params.set("format", format);
  }
  return params.toString();
}

function LogPage() {
  const [eventType, setEventType] = useState("temperature_sample");
  const [sourceType, setSourceType] = useState("");
  const [downloadLimit, setDownloadLimit] = useState(86400);
  const [items, setItems] = useState([]);
  const [stats, setStats] = useState(null);
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(false);

  const previewQuery = useMemo(
    () => buildQuery({ eventType, sourceType, limit: 50 }),
    [eventType, sourceType],
  );
  const temperatureRows = useMemo(() => buildTemperatureRows(items), [items]);
  const relayRows = useMemo(() => buildRelayRows(items), [items]);
  const tvRows = useMemo(() => buildTvRows(items), [items]);

  useEffect(() => {
    let cancelled = false;

    async function loadLogs() {
      setLoading(true);
      setError("");
      try {
        const [historyResponse, statsResponse] = await Promise.all([
          fetch(getApiBaseUrl(`/api/history?${previewQuery}`)),
          fetch(getApiBaseUrl("/api/history/stats")),
        ]);
        if (!historyResponse.ok) {
          throw new Error(`history ${historyResponse.status}`);
        }
        if (!statsResponse.ok) {
          throw new Error(`stats ${statsResponse.status}`);
        }
        const historyPayload = await historyResponse.json();
        const statsPayload = await statsResponse.json();
        if (!cancelled) {
          setItems(Array.isArray(historyPayload.items) ? historyPayload.items : []);
          setStats(statsPayload);
        }
      } catch (loadError) {
        if (!cancelled) {
          setError(loadError.message || "Cannot load logs");
          setItems([]);
        }
      } finally {
        if (!cancelled) {
          setLoading(false);
        }
      }
    }

    loadLogs();
    const timerId = window.setInterval(loadLogs, 5000);
    return () => {
      cancelled = true;
      window.clearInterval(timerId);
    };
  }, [previewQuery]);

  function download(format) {
    const query = buildQuery({
      eventType,
      sourceType,
      limit: downloadLimit,
      format,
    });
    const link = document.createElement("a");
    link.href = getApiBaseUrl(`/api/history/export?${query}`);
    link.rel = "noopener";
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  }

  return (
    <>
      <section className="log-header-card">
        <div>
          <span className="eyebrow">Logs</span>
          <h2>Export database logs</h2>
          <p>Download temperature samples, relay/control events and TV app sessions from the local SQLite database.</p>
        </div>
        <div className="log-stat-strip">
          <div>
            <span>Events</span>
            <strong>{stats?.approxEventCount ?? "-"}</strong>
          </div>
          <div>
            <span>DB Size</span>
            <strong>{stats?.dbSizeBytes ? `${Math.round(stats.dbSizeBytes / 1024)} KB` : "-"}</strong>
          </div>
        </div>
      </section>

      <section className="log-control-card">
        <div className="log-filter-grid">
          <label>
            <span>Log type</span>
            <select value={eventType} onChange={(event) => setEventType(event.target.value)}>
              {eventTypeOptions.map((option) => (
                <option key={option.value || "all"} value={option.value}>
                  {option.label}
                </option>
              ))}
            </select>
          </label>
          <label>
            <span>Source</span>
            <select value={sourceType} onChange={(event) => setSourceType(event.target.value)}>
              {sourceOptions.map((option) => (
                <option key={option.value || "all"} value={option.value}>
                  {option.label}
                </option>
              ))}
            </select>
          </label>
          <label>
            <span>Rows to download</span>
            <input
              type="number"
              min="1"
              max="200000"
              step="100"
              value={downloadLimit}
              onChange={(event) => setDownloadLimit(Number(event.target.value) || 1)}
            />
          </label>
          <div className="log-download-actions" aria-label="Download logs">
            <button className="domain-link" type="button" onClick={() => download("csv")}>
              CSV
            </button>
            <button className="ghost-pill" type="button" onClick={() => download("json")}>
              JSON
            </button>
          </div>
        </div>
      </section>

      <section className="log-table-card">
        <div className="timeline-header">
          <div>
            <span className="eyebrow">Preview</span>
            <h3>{loading ? "Loading recent logs..." : "Latest 50 matching events"}</h3>
          </div>
        </div>
        {error ? <p className="log-error">{error}</p> : null}
        {eventType === "temperature_sample" ? (
          <div className="tv-app-history-table-wrap">
            <table className="tv-app-history-table">
              <thead>
                <tr>
                  <th>STT</th>
                  <th>Time</th>
                  <th>Sensor</th>
                  <th>Temp</th>
                  <th>Raw</th>
                </tr>
              </thead>
              <tbody>
                {temperatureRows.map((row) => (
                  <tr key={row.id}>
                    <td>{row.stt}</td>
                    <td>{formatDateTime(row.time)}</td>
                    <td>{row.sensor}</td>
                    <td>{row.temp}</td>
                    <td>{row.raw}</td>
                  </tr>
                ))}
                {!temperatureRows.length && !loading ? (
                  <tr>
                    <td colSpan="5">No matching logs.</td>
                  </tr>
                ) : null}
              </tbody>
            </table>
          </div>
        ) : isRelayEventType(eventType) ? (
          <div className="tv-app-history-table-wrap">
            <table className="tv-app-history-table">
              <thead>
                <tr>
                  <th>STT</th>
                  <th>Time</th>
                  <th>Source</th>
                  <th>Relay</th>
                  <th>State</th>
                </tr>
              </thead>
              <tbody>
                {relayRows.map((row) => (
                  <tr key={row.id}>
                    <td>{row.stt}</td>
                    <td>{formatDateTime(row.time)}</td>
                    <td>{row.source}</td>
                    <td>{row.relay}</td>
                    <td>{String(row.state)}</td>
                  </tr>
                ))}
                {!relayRows.length && !loading ? (
                  <tr>
                    <td colSpan="5">No matching logs.</td>
                  </tr>
                ) : null}
              </tbody>
            </table>
          </div>
        ) : eventType === "app_session" ? (
          <div className="tv-app-history-table-wrap">
            <table className="tv-app-history-table">
              <thead>
                <tr>
                  <th>STT</th>
                  <th>App</th>
                  <th>Started At</th>
                  <th>Ended At</th>
                </tr>
              </thead>
              <tbody>
                {tvRows.map((row) => (
                  <tr key={row.id}>
                    <td>{row.stt}</td>
                    <td>{row.app}</td>
                    <td>{formatDateTime(row.startedAt)}</td>
                    <td>{formatDateTime(row.endedAt)}</td>
                  </tr>
                ))}
                {!tvRows.length && !loading ? (
                  <tr>
                    <td colSpan="4">No matching logs.</td>
                  </tr>
                ) : null}
              </tbody>
            </table>
          </div>
        ) : (
          <div className="tv-app-history-table-wrap">
            <table className="tv-app-history-table">
              <thead>
                <tr>
                  <th>Time</th>
                  <th>Source</th>
                  <th>Type</th>
                  <th>Payload</th>
                </tr>
              </thead>
              <tbody>
                {items.map((item) => (
                  <tr key={item.id}>
                    <td>{formatDateTime(item.ts)}</td>
                    <td>{item.sourceId}</td>
                    <td>{item.eventType}</td>
                    <td className="log-payload-cell">{formatPayload(item)}</td>
                  </tr>
                ))}
                {!items.length && !loading ? (
                  <tr>
                    <td colSpan="4">No matching logs.</td>
                  </tr>
                ) : null}
              </tbody>
            </table>
          </div>
        )}
      </section>
    </>
  );
}

export default LogPage;
