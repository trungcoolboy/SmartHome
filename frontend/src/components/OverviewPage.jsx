import React, { useEffect, useMemo, useState } from "react";
import { getApiBaseUrl } from "../api";
import {
  overviewModules,
  overviewStats,
  pinnedDevices,
  quickActions,
  systemPulse,
  timelineEvents,
} from "./mockData";

function formatLastSeen(lastSeen) {
  if (!lastSeen) {
    return "No recent sync";
  }
  return new Date(lastSeen * 1000).toLocaleTimeString();
}

function getServiceState(health, prefix) {
  return health?.services?.[prefix]?.payload?.state ?? null;
}

function OverviewPage({ onNavigate }) {
  const [health, setHealth] = useState(null);
  const [healthError, setHealthError] = useState("");

  useEffect(() => {
    let cancelled = false;
    let timerId = null;

    async function loadHealth(options = {}) {
      const preserveError = options.preserveError === true;
      try {
        const response = await fetch(getApiBaseUrl("/health"));
        if (!response.ok) {
          throw new Error(`health ${response.status}`);
        }
        const payload = await response.json();
        if (!cancelled) {
          setHealth(payload);
          if (!preserveError) {
            setHealthError("");
          }
        }
      } catch (error) {
        if (!cancelled) {
          setHealthError(error.message);
        }
      }
    }

    loadHealth();
    timerId = window.setInterval(() => loadHealth({ preserveError: true }), 15000);

    return () => {
      cancelled = true;
      if (timerId) {
        window.clearInterval(timerId);
      }
    };
  }, []);

  const derived = useMemo(() => {
    const tvState = getServiceState(health, "/api/tv/living-room");
    const roomNode01 = getServiceState(health, "/api/node/living-room-01");
    const roomNode02 = getServiceState(health, "/api/node/living-room-02");
    const stm32States = [
      getServiceState(health, "/api/stm32/01"),
      getServiceState(health, "/api/stm32/02"),
      getServiceState(health, "/api/stm32/03"),
    ];

    const tvOnline = Boolean(tvState?.reachable);
    const pairedTv = Boolean(tvState?.paired);
    const tvStale = Boolean(tvState?.stale);
    const tvWakePending = Boolean(tvState?.wakePending);
    const connectedBoards = stm32States.filter((item) => item?.connected).length;
    const totalBoards = stm32States.filter(Boolean).length;
    const boardErrors = stm32States.filter((item) => item?.error).length;
    const roomNodeOnlineCount = [roomNode01, roomNode02].filter((item) => item?.connected).length;
    const totalRoomNodes = [roomNode01, roomNode02].filter(Boolean).length;
    const onlineCount = connectedBoards + roomNodeOnlineCount + (tvOnline ? 1 : 0);
    const totalKnownDevices = totalBoards + totalRoomNodes + 1;

    return {
      tvState,
      roomNode01,
      roomNode02,
      stm32States,
      tvOnline,
      pairedTv,
      tvStale,
      tvWakePending,
      connectedBoards,
      totalBoards,
      boardErrors,
      roomNodeOnlineCount,
      totalRoomNodes,
      onlineCount,
      totalKnownDevices,
    };
  }, [health]);

  const liveSystemPulse = [
    {
      label: "Smart Home API",
      value: health?.ok ? "Online" : "Degraded",
      detail: healthError || "Gateway on 8090 routing TV and STM32 services",
      tone: health?.ok ? "good" : "warn",
    },
    {
      label: "Living Room TV",
      value: derived.tvWakePending
        ? "Wake pending"
        : derived.tvOnline
          ? derived.tvStale
            ? "Stale"
            : "Online"
          : "Offline",
      detail: derived.tvState
        ? derived.tvState.foregroundAppId || `Last seen ${formatLastSeen(derived.tvState.lastSeen)}`
        : "Waiting for gateway payload",
      tone: derived.tvWakePending ? "accent" : derived.tvOnline ? "accent" : "warn",
    },
    {
      label: "Aqua Cluster",
      value: `${derived.connectedBoards}/${derived.totalBoards || 3} bridges up`,
      detail:
        derived.boardErrors > 0
          ? `${derived.boardErrors} bridge(s) reporting serial errors`
          : "All discovered STM32 bridges are healthy",
      tone: derived.boardErrors > 0 ? "warn" : "good",
    },
  ];

  const livePinnedDevices = [
    {
      name: "Living Room TV",
      status: derived.tvWakePending ? "Wake pending" : derived.tvOnline ? "Online" : "Offline",
      meta: derived.tvState?.foregroundAppId || (derived.tvStale ? "Cached state retained" : "No active source reported"),
      tone: derived.tvWakePending ? "accent" : derived.tvOnline ? "accent" : "warn",
    },
    {
      name: "Living Room Node 01",
      status: derived.roomNode01?.connected ? "Linked" : "Down",
      meta: derived.roomNode01?.ip
        ? `IP ${derived.roomNode01.ip} / RSSI ${derived.roomNode01.wifiRssi ?? "n/a"}`
        : derived.roomNode01?.lastError || "No node state",
      tone: derived.roomNode01?.connected ? "good" : "warn",
    },
    {
      name: "Living Room Node 02",
      status: derived.roomNode02?.connected ? "Linked" : "Down",
      meta: derived.roomNode02?.ip
        ? `IP ${derived.roomNode02.ip} / RSSI ${derived.roomNode02.wifiRssi ?? "n/a"}`
        : derived.roomNode02?.lastError || "No node state",
      tone: derived.roomNode02?.connected ? "good" : "warn",
    },
    {
      name: "STM32 #01",
      status: derived.stm32States[0]?.connected ? "Linked" : "Down",
      meta: derived.stm32States[0]?.error || derived.stm32States[0]?.serialDevice || "No bridge state",
      tone: derived.stm32States[0]?.connected ? "good" : "warn",
    },
    {
      name: "STM32 #02",
      status: derived.stm32States[1]?.connected ? "Linked" : "Down",
      meta: derived.stm32States[1]?.error || derived.stm32States[1]?.serialDevice || "No bridge state",
      tone: derived.stm32States[1]?.connected ? "good" : "warn",
    },
    {
      name: "STM32 #03",
      status: derived.stm32States[2]?.connected ? "Linked" : "Down",
      meta: derived.stm32States[2]?.error || derived.stm32States[2]?.serialDevice || "No bridge state",
      tone: derived.stm32States[2]?.connected ? "good" : "warn",
    },
  ];

  const liveOverviewStats = [
    {
      label: "Devices Online",
      value: `${derived.onlineCount}/${derived.totalKnownDevices || 4}`,
      tone: derived.onlineCount >= 3 ? "good" : "warn",
    },
    {
      label: "Open Alerts",
      value: String(derived.boardErrors + (derived.tvOnline ? 0 : 1)).padStart(2, "0"),
      tone: derived.boardErrors + (derived.tvOnline ? 0 : 1) > 0 ? "warn" : "good",
    },
    {
      label: "TV State",
      value: derived.tvWakePending ? "Waking" : derived.pairedTv ? "Paired" : derived.tvOnline ? "Reachable" : "Offline",
      tone: derived.tvWakePending ? "accent" : derived.pairedTv ? "accent" : derived.tvOnline ? "neutral" : "warn",
    },
    {
      label: "Aqua Bridges",
      value: `${derived.connectedBoards}/${derived.totalBoards || 3}`,
      tone: derived.connectedBoards === derived.totalBoards && derived.totalBoards > 0 ? "good" : "warn",
    },
  ];

  const liveOverviewModules = overviewModules.map((section) => {
    if (section.id === "living-room") {
      return {
        ...section,
        metrics: [
          { label: "TV", value: derived.tvOnline ? "Online" : "Offline" },
          { label: "Node 01", value: derived.roomNode01?.connected ? "Online" : "Offline" },
          { label: "Node 02", value: derived.roomNode02?.connected ? "Online" : "Offline" },
          { label: "Pairing", value: derived.tvWakePending ? "Waking" : derived.pairedTv ? "Ready" : "Pending" },
          { label: "Source", value: derived.tvState?.foregroundAppId || "No source" },
        ],
      };
    }

    if (section.id === "aquarium") {
      return {
        ...section,
        metrics: [
          { label: "Bridges", value: `${derived.connectedBoards}/${derived.totalBoards || 3} linked` },
          { label: "Core", value: derived.stm32States[0]?.connected ? "Online" : "Offline" },
          {
            label: "Motion / Light",
            value:
              derived.stm32States[1]?.connected || derived.stm32States[2]?.connected
                ? "Available"
                : "Offline",
          },
        ],
      };
    }

    return section;
  });

  const liveTimelineEvents = [
    {
      time: new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }),
      title: "Gateway health refresh",
      detail: healthError || (health?.ok ? "Smart Home API reported healthy downstream routes." : "Gateway health is degraded."),
    },
    {
      time: derived.tvState?.lastSeen ? formatLastSeen(derived.tvState.lastSeen) : "--:--",
      title: "Living Room TV",
      detail: derived.tvState
        ? `${derived.tvWakePending ? "Wake pending" : derived.tvOnline ? derived.tvStale ? "Stale" : "Reachable" : "Offline"} / ${derived.tvState.foregroundAppId || "no active app"}`
        : "No TV status received from gateway.",
    },
    {
      time: "--:--",
      title: "Aqua bridge summary",
      detail:
        derived.boardErrors > 0
          ? `${derived.boardErrors} STM32 bridge(s) currently reporting serial errors.`
          : `${derived.connectedBoards}/${derived.totalBoards || 3} STM32 bridge(s) currently linked.`,
    },
  ];

  return (
    <>
      <section className="hero-card">
        <div className="hero-copy">
          <span className="eyebrow">Overview</span>
          <h2>One interface for rooms, water systems and motion control.</h2>
          <p>
            The visual language is based on iOS/HomeKit cards, but the information
            density is tuned for pump, relay, sensor and actuator control.
          </p>

          <div className="hero-pulse-grid">
            {liveSystemPulse.map((item) => (
              <article key={item.label} className={`pulse-card tone-${item.tone}`}>
                <span>{item.label}</span>
                <strong>{item.value}</strong>
                <p>{item.detail}</p>
              </article>
            ))}
          </div>
        </div>

        <div className="hero-actions">
          <div className="hero-panel">
            <span className="panel-kicker">Pinned Devices</span>
            <div className="hero-device-list">
              {livePinnedDevices.map((device) => (
                <div key={device.name} className={`hero-device-card tone-${device.tone}`}>
                  <div>
                    <strong>{device.name}</strong>
                    <p>{device.meta}</p>
                  </div>
                  <span>{device.status}</span>
                </div>
              ))}
            </div>
          </div>

          {quickActions.map((action, index) => (
            <button key={action} className={index === 0 ? "action-pill primary" : "action-pill"} type="button">
              {action}
            </button>
          ))}
        </div>
      </section>

      <section className="stats-grid" aria-label="System stats">
        {liveOverviewStats.map((card) => (
          <article key={card.label} className={`stat-card tone-${card.tone}`}>
            <span>{card.label}</span>
            <strong>{card.value}</strong>
          </article>
        ))}
      </section>

      <section className="section-grid" aria-label="Domain sections">
        {liveOverviewModules.map((section) => (
          <article key={section.id} className="domain-card">
            <div className="domain-header">
              <span className="eyebrow">{section.title}</span>
              <h3>{section.description}</h3>
            </div>

            <div className="metric-list">
              {section.metrics.map((metric) => (
                <div key={metric.label} className="metric-row">
                  <span>{metric.label}</span>
                  <strong>{metric.value}</strong>
                </div>
              ))}
            </div>

            <button className="domain-link" type="button" onClick={() => onNavigate(section.id)}>
              Open module
            </button>
          </article>
        ))}
      </section>

      <section className="timeline-card" aria-label="Recent timeline">
        <div className="timeline-header">
          <div>
            <span className="eyebrow">Realtime Timeline</span>
            <h3>Recent system activity across rooms and aqua modules.</h3>
          </div>
          <button className="ghost-pill" type="button" onClick={() => onNavigate("alerts")}>
            Open Alerts
          </button>
        </div>

        <div className="timeline-grid">
          {liveTimelineEvents.map((event) => (
            <article key={`${event.time}-${event.title}`} className="timeline-item">
              <span>{event.time}</span>
              <strong>{event.title}</strong>
              <p>{event.detail}</p>
            </article>
          ))}
        </div>
      </section>
    </>
  );
}

export default OverviewPage;
