import React, { useEffect, useState } from "react";
import { getApiBaseUrl } from "../api";

const defaultForm = {
  rtmpUrl: "rtmp://a.rtmp.youtube.com/live2",
  streamKey: "",
  videoDevice: "/dev/v4l/by-id/usb-046d_HD_Pro_Webcam_C920_2EA9AABF-video-index0",
  audioDevice: "hw:2,0",
  audioMode: "webcam",
  resolution: "1280x720",
  framerate: 30,
  videoBitrateKbps: 3000,
  audioBitrateKbps: 128,
};

function formatDateTime(valueSeconds) {
  if (!Number.isFinite(Number(valueSeconds))) {
    return "-";
  }
  return new Date(Number(valueSeconds) * 1000).toLocaleString();
}

function LivestreamPage() {
  const [payload, setPayload] = useState(null);
  const [form, setForm] = useState(defaultForm);
  const [loading, setLoading] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [previewVersion, setPreviewVersion] = useState(Date.now());

  function applyConfig(config) {
    if (!config) {
      return;
    }
    setForm((current) => ({
      ...current,
      ...config,
      streamKey: "",
    }));
  }

  async function loadLivestream({ quiet = false } = {}) {
    if (!quiet) {
      setLoading(true);
    }
    setError("");
    try {
      const response = await fetch(getApiBaseUrl("/api/livestream"));
      if (!response.ok) {
        throw new Error(`livestream ${response.status}`);
      }
      const nextPayload = await response.json();
      setPayload(nextPayload);
      applyConfig(nextPayload.config);
    } catch (loadError) {
      setError(loadError.message || "Cannot load livestream");
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
        const response = await fetch(getApiBaseUrl("/api/livestream"));
        if (!response.ok) {
          throw new Error(`livestream ${response.status}`);
        }
        const nextPayload = await response.json();
        if (!cancelled) {
          setPayload(nextPayload);
          applyConfig(nextPayload.config);
        }
      } catch (loadError) {
        if (!cancelled) {
          setError(loadError.message || "Cannot load livestream");
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
        loadLivestream({ quiet: true });
        setPreviewVersion(Date.now());
      }
    }, 3000);
    return () => {
      cancelled = true;
      window.clearInterval(timerId);
    };
  }, []);

  function updateField(field, value) {
    setForm((current) => ({ ...current, [field]: value }));
  }

  function buildConfigForSubmit() {
    const config = {
      ...form,
      framerate: Number(form.framerate),
      videoBitrateKbps: Number(form.videoBitrateKbps),
      audioBitrateKbps: Number(form.audioBitrateKbps),
    };
    if (!String(config.streamKey || "").trim()) {
      delete config.streamKey;
    }
    return config;
  }

  async function sendAction(action) {
    setBusy(true);
    setError("");
    try {
      const response = await fetch(getApiBaseUrl("/api/livestream"), {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ action, config: buildConfigForSubmit() }),
      });
      const nextPayload = await response.json().catch(() => null);
      if (nextPayload) {
        setPayload(nextPayload);
        applyConfig(nextPayload.config);
      }
      if (!response.ok) {
        throw new Error(nextPayload?.error || `${action} failed`);
      }
    } catch (actionError) {
      setError(actionError.message || "Livestream command failed");
    } finally {
      setBusy(false);
      loadLivestream({ quiet: true });
    }
  }

  const running = Boolean(payload?.running);
  const streamKeySet = Boolean(payload?.config?.streamKeySet);

  return (
    <>
      <section className="statistics-header-card">
        <div>
          <span className="eyebrow">Livestream</span>
          <h2>YouTube webcam</h2>
        </div>
        <div className="livestream-actions">
          <button className="ghost-pill" type="button" onClick={() => sendAction("save")} disabled={busy}>
            Save
          </button>
          <button className="domain-link" type="button" onClick={() => sendAction("start")} disabled={busy || running}>
            {running ? "Live" : "Start"}
          </button>
          <button className="ghost-pill" type="button" onClick={() => sendAction("stop")} disabled={busy || !running}>
            Stop
          </button>
        </div>
      </section>

      {error ? <p className="log-error">{error}</p> : null}

      <section className="statistics-grid">
        <article className="statistics-card livestream-preview-card">
          <div className="timeline-header">
            <div>
              <span className="eyebrow">Preview</span>
              <h3>Webcam</h3>
            </div>
          </div>
          <img
            className="livestream-preview-image"
            src={getApiBaseUrl(`/api/livestream/preview.jpg?t=${previewVersion}`)}
            alt="Webcam preview"
          />
        </article>

        <article className="statistics-card">
          <div className="timeline-header">
            <div>
              <span className="eyebrow">Status</span>
              <h3>{loading ? "Loading..." : running ? "Streaming" : "Stopped"}</h3>
            </div>
          </div>
          <div className="backup-status-grid">
            <div>
              <span>PID</span>
              <strong>{payload?.pid ?? "-"}</strong>
            </div>
            <div>
              <span>Started</span>
              <strong>{formatDateTime(payload?.startedAt)}</strong>
            </div>
            <div>
              <span>Stream key</span>
              <strong>{streamKeySet ? "Saved" : "Missing"}</strong>
            </div>
            <div>
              <span>Return code</span>
              <strong>{payload?.returnCode ?? "-"}</strong>
            </div>
          </div>
        </article>

        <article className="statistics-card">
          <div className="timeline-header">
            <div>
              <span className="eyebrow">Output</span>
              <h3>YouTube RTMP</h3>
            </div>
          </div>
          <div className="livestream-form-grid">
            <label>
              <span>RTMP URL</span>
              <input value={form.rtmpUrl} onChange={(event) => updateField("rtmpUrl", event.target.value)} />
            </label>
            <label>
              <span>Stream Key</span>
              <input
                type="password"
                value={form.streamKey}
                placeholder={streamKeySet ? "Saved, leave blank to keep" : "Paste YouTube stream key"}
                onChange={(event) => updateField("streamKey", event.target.value)}
              />
            </label>
          </div>
        </article>
      </section>

      <section className="statistics-card statistics-card-wide">
        <div className="timeline-header">
          <div>
            <span className="eyebrow">Input</span>
            <h3>Webcam and encoder</h3>
          </div>
        </div>
        <div className="livestream-form-grid livestream-form-grid-wide">
          <label>
            <span>Video Device</span>
            <input value={form.videoDevice} onChange={(event) => updateField("videoDevice", event.target.value)} />
          </label>
          <label>
            <span>Audio Mode</span>
            <select value={form.audioMode} onChange={(event) => updateField("audioMode", event.target.value)}>
              <option value="webcam">Webcam mic</option>
              <option value="silent">Silent audio</option>
            </select>
          </label>
          <label>
            <span>Audio Device</span>
            <input value={form.audioDevice} onChange={(event) => updateField("audioDevice", event.target.value)} />
          </label>
          <label>
            <span>Resolution</span>
            <select value={form.resolution} onChange={(event) => updateField("resolution", event.target.value)}>
              <option value="1280x720">1280x720</option>
              <option value="1920x1080">1920x1080</option>
              <option value="640x480">640x480</option>
            </select>
          </label>
          <label>
            <span>FPS</span>
            <input type="number" min="10" max="60" value={form.framerate} onChange={(event) => updateField("framerate", event.target.value)} />
          </label>
          <label>
            <span>Video Kbps</span>
            <input type="number" min="500" max="9000" value={form.videoBitrateKbps} onChange={(event) => updateField("videoBitrateKbps", event.target.value)} />
          </label>
          <label>
            <span>Audio Kbps</span>
            <input type="number" min="64" max="320" value={form.audioBitrateKbps} onChange={(event) => updateField("audioBitrateKbps", event.target.value)} />
          </label>
        </div>
      </section>

      <section className="statistics-card statistics-card-wide">
        <div className="timeline-header">
          <div>
            <span className="eyebrow">FFmpeg</span>
            <h3>Recent log</h3>
          </div>
        </div>
        <pre className="backup-log">{(payload?.logs ?? []).join("\n") || "No livestream log yet."}</pre>
      </section>
    </>
  );
}

export default LivestreamPage;
