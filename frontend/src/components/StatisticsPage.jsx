import React, { useEffect, useMemo, useState } from "react";
import { getApiBaseUrl } from "../api";

const chartColors = ["#77e0cf", "#ffbc7d", "#9bd9ff", "#ff8d8d", "#c5fff5", "#75e39a"];

function formatDuration(seconds) {
  const totalSeconds = Math.max(0, Math.round(Number(seconds) || 0));
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  if (hours) {
    return `${hours}h ${String(minutes).padStart(2, "0")}m`;
  }
  return `${minutes}m`;
}

function formatTime(unixSeconds) {
  if (!Number.isFinite(Number(unixSeconds))) {
    return "-";
  }
  return new Date(Number(unixSeconds) * 1000).toLocaleTimeString([], {
    hour: "2-digit",
    minute: "2-digit",
  });
}

function TemperatureChart({ series, startTs, endTs }) {
  const chart = useMemo(() => {
    const points = series.flatMap((item) => item.points ?? []);
    if (!points.length) {
      return null;
    }

    const temps = points.map((point) => Number(point.temp)).filter(Number.isFinite);
    const minTemp = Math.min(...temps);
    const maxTemp = Math.max(...temps);
    const yMin = Math.floor(minTemp - 0.5);
    const yMax = Math.ceil(maxTemp + 0.5);
    const width = 900;
    const height = 320;
    const padLeft = 54;
    const padRight = 18;
    const padTop = 20;
    const padBottom = 38;
    const plotWidth = width - padLeft - padRight;
    const plotHeight = height - padTop - padBottom;
    const xSpan = Math.max(1, endTs - startTs);
    const ySpan = Math.max(0.1, yMax - yMin);

    function x(ts) {
      return padLeft + ((Number(ts) - startTs) / xSpan) * plotWidth;
    }

    function y(temp) {
      return padTop + (1 - ((Number(temp) - yMin) / ySpan)) * plotHeight;
    }

    const lines = series.map((item, index) => {
      const path = (item.points ?? [])
        .filter((point) => Number.isFinite(Number(point.ts)) && Number.isFinite(Number(point.temp)))
        .map((point) => `${x(point.ts).toFixed(1)},${y(point.temp).toFixed(1)}`)
        .join(" ");
      return {
        sensor: item.sensor,
        color: chartColors[index % chartColors.length],
        path,
      };
    });

    const yTicks = Array.from({ length: 5 }, (_, index) => yMin + ((ySpan / 4) * index));
    const xTicks = Array.from({ length: 5 }, (_, index) => startTs + ((xSpan / 4) * index));
    return { width, height, padLeft, padRight, padTop, padBottom, plotWidth, plotHeight, yMin, yMax, lines, yTicks, xTicks, x, y };
  }, [series, startTs, endTs]);

  if (!chart) {
    return <div className="statistics-empty">No temperature data in this window.</div>;
  }

  return (
    <div className="temperature-chart-wrap">
      <svg
        className="temperature-chart"
        viewBox={`0 0 ${chart.width} ${chart.height}`}
        role="img"
        aria-label="Temperature chart"
      >
        <rect
          x={chart.padLeft}
          y={chart.padTop}
          width={chart.plotWidth}
          height={chart.plotHeight}
          rx="8"
        />
        {chart.yTicks.map((tick) => {
          const y = chart.y(tick);
          return (
            <g key={`y-${tick}`}>
              <line x1={chart.padLeft} x2={chart.width - chart.padRight} y1={y} y2={y} />
              <text x={chart.padLeft - 10} y={y + 4} textAnchor="end">
                {tick.toFixed(1)}C
              </text>
            </g>
          );
        })}
        {chart.xTicks.map((tick) => {
          const x = chart.x(tick);
          return (
            <text key={`x-${tick}`} x={x} y={chart.height - 12} textAnchor="middle">
              {formatTime(tick)}
            </text>
          );
        })}
        {chart.lines.map((line) =>
          line.path ? (
            <polyline key={line.sensor} points={line.path} stroke={line.color} />
          ) : null,
        )}
      </svg>
      <div className="temperature-legend">
        {chart.lines.map((line) => (
          <span key={line.sensor}>
            <i style={{ background: line.color }} />
            {line.sensor}
          </span>
        ))}
      </div>
    </div>
  );
}

function StatisticsPage() {
  const [hours, setHours] = useState(24);
  const [payload, setPayload] = useState(null);
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    let cancelled = false;

    async function loadStatistics() {
      setLoading(true);
      setError("");
      try {
        const response = await fetch(getApiBaseUrl(`/api/statistics?hours=${hours}`));
        if (!response.ok) {
          throw new Error(`statistics ${response.status}`);
        }
        const nextPayload = await response.json();
        if (!cancelled) {
          setPayload(nextPayload);
        }
      } catch (loadError) {
        if (!cancelled) {
          setError(loadError.message || "Cannot load statistics");
        }
      } finally {
        if (!cancelled) {
          setLoading(false);
        }
      }
    }

    loadStatistics();
    const timerId = window.setInterval(loadStatistics, 30000);
    return () => {
      cancelled = true;
      window.clearInterval(timerId);
    };
  }, [hours]);

  const relays = payload?.relays ?? [];
  const tvApps = payload?.tvApps ?? [];
  const temperatures = payload?.temperatures ?? [];

  return (
    <>
      <section className="statistics-header-card">
        <div>
          <span className="eyebrow">Statistics</span>
          <h2>Database statistics</h2>
          <p>Relay ON time, TV App time and 24h temperature history from local logs.</p>
        </div>
        <label className="statistics-window-picker">
          <span>Window</span>
          <select value={hours} onChange={(event) => setHours(Number(event.target.value))}>
            <option value={6}>6h</option>
            <option value={12}>12h</option>
            <option value={24}>24h</option>
            <option value={72}>3d</option>
            <option value={168}>7d</option>
          </select>
        </label>
      </section>

      {error ? <p className="log-error">{error}</p> : null}

      <section className="statistics-grid">
        <article className="statistics-card">
          <div className="timeline-header">
            <div>
              <span className="eyebrow">Relay</span>
              <h3>{loading ? "Loading..." : "ON time"}</h3>
            </div>
          </div>
          <div className="statistics-list">
            {relays.map((item) => (
              <div key={`${item.source}-${item.relay}`} className="statistics-row">
                <div>
                  <strong>{item.relay}</strong>
                  <span>{item.source}</span>
                </div>
                <b>{formatDuration(item.secondsOn)}</b>
              </div>
            ))}
            {!relays.length && !loading ? <div className="statistics-empty">No relay data.</div> : null}
          </div>
        </article>

        <article className="statistics-card">
          <div className="timeline-header">
            <div>
              <span className="eyebrow">TV App</span>
              <h3>{loading ? "Loading..." : "Run time"}</h3>
            </div>
          </div>
          <div className="statistics-list">
            {tvApps.map((item) => (
              <div key={item.app} className="statistics-row">
                <div>
                  <strong>{item.app}</strong>
                  <span>TV App</span>
                </div>
                <b>{formatDuration(item.secondsOn)}</b>
              </div>
            ))}
            {!tvApps.length && !loading ? <div className="statistics-empty">No TV App data.</div> : null}
          </div>
        </article>
      </section>

      <section className="statistics-card statistics-card-wide">
        <div className="timeline-header">
          <div>
            <span className="eyebrow">Temperature</span>
            <h3>Sensor chart</h3>
          </div>
        </div>
        <TemperatureChart
          series={temperatures}
          startTs={payload?.startTs ?? 0}
          endTs={payload?.endTs ?? 1}
        />
        <div className="temperature-summary-grid">
          {temperatures.map((item) => (
            <div key={item.sensor}>
              <span>{item.sensor}</span>
              <strong>{Number(item.latest).toFixed(2)}C</strong>
              <small>
                min {Number(item.min).toFixed(2)} / max {Number(item.max).toFixed(2)} / avg{" "}
                {Number(item.avg).toFixed(2)}
              </small>
            </div>
          ))}
        </div>
      </section>
    </>
  );
}

export default StatisticsPage;
