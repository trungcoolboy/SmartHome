import React, { useEffect, useMemo, useState } from "react";
import { getApiBaseUrl } from "../api";

function formatDateTime(valueSeconds) {
  if (!Number.isFinite(Number(valueSeconds))) {
    return "-";
  }
  return new Date(Number(valueSeconds) * 1000).toLocaleString();
}

function formatSize(bytes) {
  const value = Number(bytes);
  if (!Number.isFinite(value) || value <= 0) {
    return "0 B";
  }
  const units = ["B", "KB", "MB", "GB"];
  let size = value;
  let index = 0;
  while (size >= 1024 && index < units.length - 1) {
    size /= 1024;
    index += 1;
  }
  return `${size.toFixed(index === 0 ? 0 : 1)} ${units[index]}`;
}

function formatSystemdTimestamp(value) {
  if (!value || value === "n/a") {
    return "-";
  }
  return value;
}

function BackupPage() {
  const [payload, setPayload] = useState(null);
  const [loading, setLoading] = useState(false);
  const [running, setRunning] = useState(false);
  const [error, setError] = useState("");

  async function loadBackups({ quiet = false } = {}) {
    if (!quiet) {
      setLoading(true);
    }
    setError("");
    try {
      const response = await fetch(getApiBaseUrl("/api/backups"));
      if (!response.ok) {
        throw new Error(`backup status ${response.status}`);
      }
      setPayload(await response.json());
    } catch (loadError) {
      setError(loadError.message || "Cannot load backups");
    } finally {
      if (!quiet) {
        setLoading(false);
      }
    }
  }

  useEffect(() => {
    let cancelled = false;

    async function loadInitial() {
      setLoading(true);
      setError("");
      try {
        const response = await fetch(getApiBaseUrl("/api/backups"));
        if (!response.ok) {
          throw new Error(`backup status ${response.status}`);
        }
        const nextPayload = await response.json();
        if (!cancelled) {
          setPayload(nextPayload);
        }
      } catch (loadError) {
        if (!cancelled) {
          setError(loadError.message || "Cannot load backups");
        }
      } finally {
        if (!cancelled) {
          setLoading(false);
        }
      }
    }

    loadInitial();
    const timerId = window.setInterval(() => {
      if (!cancelled) {
        loadBackups({ quiet: true });
      }
    }, 1500);
    return () => {
      cancelled = true;
      window.clearInterval(timerId);
    };
  }, []);

  async function runBackupNow() {
    setRunning(true);
    setError("");
    try {
      const response = await fetch(getApiBaseUrl("/api/backups"), { method: "POST" });
      const nextPayload = await response.json().catch(() => null);
      if (nextPayload) {
        setPayload(nextPayload);
      }
      if (!response.ok) {
        throw new Error(nextPayload?.error || `backup run ${response.status}`);
      }
    } catch (runError) {
      setError(runError.message || "Cannot run backup");
    } finally {
      setRunning(false);
      loadBackups({ quiet: true });
    }
  }

  const backups = payload?.items ?? [];
  const latestBackup = backups[0] ?? null;
  const serviceResult = payload?.service?.Result || "-";
  const serviceExit = payload?.service?.ExecMainStatus || "-";
  const nextRun = payload?.timer?.NextElapseUSecRealtime;
  const lastRun = payload?.service?.ExecMainExitTimestamp || payload?.timer?.LastTriggerUSec;
  const diskUsage = payload?.diskUsage ?? null;
  const usedPercent = Math.max(0, Math.min(100, Number(diskUsage?.usedPercent) || 0));
  const backupProgress = payload?.progress ?? null;
  const backupProgressPercent = Math.max(0, Math.min(100, Number(backupProgress?.percent) || 0));
  const lastStatusLabel = useMemo(() => {
    if (payload?.running) {
      return "Running";
    }
    if (payload?.lastSuccessful) {
      return "Successful";
    }
    if (serviceResult && serviceResult !== "-" && serviceResult !== "success") {
      return "Failed";
    }
    return "Unknown";
  }, [payload, serviceResult]);
  const backupProgressStage = backupProgress?.stage || (payload?.running ? "Running" : lastStatusLabel);

  return (
    <>
      <section className="statistics-header-card">
        <div>
          <span className="eyebrow">Backup</span>
          <h2>USB backup</h2>
        </div>
        <button className="domain-link" type="button" onClick={runBackupNow} disabled={running || payload?.running}>
          {running || payload?.running ? "Running..." : "Run Backup Now"}
        </button>
      </section>

      {error ? <p className="log-error">{error}</p> : null}

      <section className="statistics-card statistics-card-wide backup-progress-card">
        <div className="timeline-header">
          <div>
            <span className="eyebrow">Backup Progress</span>
            <h3>{backupProgressStage}</h3>
          </div>
          <strong className="backup-progress-percent">{backupProgressPercent.toFixed(0)}%</strong>
        </div>
        <div className="backup-progress-meter backup-progress-meter-large" aria-label="Backup progress">
          <span style={{ width: `${backupProgressPercent}%` }} />
        </div>
      </section>

      <section className="statistics-grid">
        <article className="statistics-card">
          <div className="timeline-header">
            <div>
              <span className="eyebrow">Status</span>
              <h3>{loading ? "Loading..." : lastStatusLabel}</h3>
            </div>
          </div>
          <div className="backup-status-grid">
            <div>
              <span>Last run</span>
              <strong>{formatSystemdTimestamp(lastRun)}</strong>
            </div>
            <div>
              <span>Next run</span>
              <strong>{formatSystemdTimestamp(nextRun)}</strong>
            </div>
            <div>
              <span>Service result</span>
              <strong>{serviceResult}</strong>
            </div>
            <div>
              <span>Exit code</span>
              <strong>{serviceExit}</strong>
            </div>
          </div>
        </article>

        <article className="statistics-card">
          <div className="timeline-header">
            <div>
              <span className="eyebrow">USB Storage</span>
              <h3>{diskUsage ? `${usedPercent.toFixed(1)}% used` : "Unknown"}</h3>
            </div>
          </div>
          <div className="backup-usage-meter" aria-label="USB storage used">
            <span style={{ width: `${usedPercent}%` }} />
          </div>
          <div className="backup-usage-details">
            <span>{formatSize(diskUsage?.usedBytes)} used</span>
            <span>{formatSize(diskUsage?.freeBytes)} free</span>
            <span>{formatSize(diskUsage?.totalBytes)} total</span>
          </div>
        </article>
      </section>

      <section className="statistics-card statistics-card-wide">
        <div className="timeline-header">
          <div>
            <span className="eyebrow">Latest</span>
            <h3>{latestBackup ? latestBackup.fileName : "No backup"}</h3>
          </div>
        </div>
        {latestBackup ? (
          <div className="backup-latest">
            <span>{formatDateTime(latestBackup.createdAt)}</span>
            <strong>{formatSize(latestBackup.sizeBytes)}</strong>
            <b className={latestBackup.successful ? "backup-ok" : "backup-fail"}>
              {latestBackup.successful ? "Successful" : "Failed"}
            </b>
          </div>
        ) : (
          <div className="statistics-empty">No USB backup archive found.</div>
        )}
      </section>

      <section className="statistics-card statistics-card-wide">
        <div className="timeline-header">
          <div>
            <span className="eyebrow">Archives</span>
            <h3>{backups.length ? `${backups.length} backup files` : "No backups"}</h3>
          </div>
        </div>
        <div className="tv-app-history-table-wrap">
          <table className="tv-app-history-table">
            <thead>
              <tr>
                <th>No.</th>
                <th>File</th>
                <th>Created</th>
                <th>Size</th>
                <th>Status</th>
              </tr>
            </thead>
            <tbody>
              {backups.map((item, index) => (
                <tr key={item.fileName}>
                  <td>{index + 1}</td>
                  <td>{item.fileName}</td>
                  <td>{formatDateTime(item.createdAt)}</td>
                  <td>{formatSize(item.sizeBytes)}</td>
                  <td>{item.successful ? "Successful" : "Failed"}</td>
                </tr>
              ))}
              {!backups.length && !loading ? (
                <tr>
                  <td colSpan="5">No matching backup files.</td>
                </tr>
              ) : null}
            </tbody>
          </table>
        </div>
      </section>

      <section className="statistics-card statistics-card-wide">
        <div className="timeline-header">
          <div>
            <span className="eyebrow">Log</span>
            <h3>Recent backup log</h3>
          </div>
        </div>
        <pre className="backup-log">{(payload?.logs ?? []).join("\n") || "No backup log yet."}</pre>
      </section>
    </>
  );
}

export default BackupPage;
