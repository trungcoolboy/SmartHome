import React, { useEffect, useMemo, useState } from "react";
import { getApiBaseUrl } from "../api";
import { overviewStats } from "./mockData";

function getServiceState(health, prefix) {
  return health?.services?.[prefix]?.payload?.state ?? null;
}

function OverviewPage() {
  const [health, setHealth] = useState(null);
  const [, setHealthError] = useState("");

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

  return (
    <>
      <section className="stats-grid" aria-label="System stats">
        {liveOverviewStats.map((card) => (
          <article key={card.label} className={`stat-card tone-${card.tone}`}>
            <span>{card.label}</span>
            <strong>{card.value}</strong>
          </article>
        ))}
      </section>

    </>
  );
}

export default OverviewPage;
