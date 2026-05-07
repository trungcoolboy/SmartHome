import React, { useEffect, useMemo, useState } from "react";
import { getApiBaseUrl } from "../api";

const eventTypeOptions = [
  { value: "", label: "All logs" },
  { value: "temperature_sample", label: "Temperature 1s" },
  { value: "relay_change", label: "Relay changed" },
  { value: "relay_command", label: "Relay command" },
  { value: "control_change", label: "STM32 control changed" },
  { value: "control_command", label: "STM32 control command" },
];

const sourceOptions = [
  { value: "", label: "All sources" },
  { value: "stm32", label: "STM32" },
  { value: "room_node", label: "Room nodes" },
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
          <p>Download temperature samples and relay/control events from the local SQLite database.</p>
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
      </section>
    </>
  );
}

export default LogPage;
