import React, { useEffect, useState } from "react";
import { getApiBaseUrl } from "./api";
import BackupPage from "./components/BackupPage";
import MobileNav from "./components/MobileNav";
import LogPage from "./components/LogPage";
import ModulePage from "./components/ModulePage";
import OverviewPage from "./components/OverviewPage";
import Sidebar from "./components/Sidebar";
import StatisticsPage from "./components/StatisticsPage";
import { alertFeed, navItems, pageContent } from "./components/mockData";

function getInitialPage() {
  const hashPage = window.location.hash.replace(/^#/, "");
  if (hashPage && navItems.some((item) => item.id === hashPage)) {
    return hashPage;
  }
  return "overview";
}

function formatHeaderTime(timestampMs) {
  return new Date(timestampMs).toLocaleTimeString([], {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
}

function formatHeaderUptime(elapsedMs) {
  const totalSeconds = Math.max(0, Math.floor(elapsedMs / 1000));
  const days = Math.floor(totalSeconds / 86400);
  const hours = Math.floor((totalSeconds % 86400) / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  const clock = [hours, minutes, seconds].map((value) => String(value).padStart(2, "0")).join(":");
  return days > 0 ? `${days}d ${clock}` : clock;
}

function App() {
  const [activePage, setActivePage] = useState(getInitialPage);
  const [headerNow, setHeaderNow] = useState(Date.now());
  const [health, setHealth] = useState(null);
  const [healthLoadedAtMs, setHealthLoadedAtMs] = useState(Date.now());
  const currentPage = pageContent[activePage];

  useEffect(() => {
    const nextHash = `#${activePage}`;
    if (window.location.hash !== nextHash) {
      window.history.replaceState(null, "", nextHash);
    }
  }, [activePage]);

  useEffect(() => {
    function handleHashChange() {
      const hashPage = window.location.hash.replace(/^#/, "");
      if (hashPage && navItems.some((item) => item.id === hashPage)) {
        setActivePage(hashPage);
      }
    }

    window.addEventListener("hashchange", handleHashChange);
    return () => window.removeEventListener("hashchange", handleHashChange);
  }, []);

  useEffect(() => {
    const timerId = window.setInterval(() => {
      setHeaderNow(Date.now());
    }, 1000);

    return () => {
      window.clearInterval(timerId);
    };
  }, []);

  useEffect(() => {
    let cancelled = false;
    let timerId = null;

    async function loadHealth() {
      try {
        const response = await fetch(getApiBaseUrl("/health"));
        if (!response.ok) {
          throw new Error(`health ${response.status}`);
        }
        const payload = await response.json();
        if (!cancelled) {
          setHealth(payload);
          setHealthLoadedAtMs(Date.now());
        }
      } catch (_error) {
        if (!cancelled) {
          setHealth(null);
        }
      }
    }

    loadHealth();
    timerId = window.setInterval(loadHealth, 15000);

    return () => {
      cancelled = true;
      if (timerId) {
        window.clearInterval(timerId);
      }
    };
  }, []);

  const effectiveServerNowMs = health?.serverTime
    ? (health.serverTime * 1000) + (headerNow - healthLoadedAtMs)
    : headerNow;
  const effectiveServerUptimeMs = Number.isFinite(health?.serverUptimeSeconds)
    ? (health.serverUptimeSeconds * 1000) + (headerNow - healthLoadedAtMs)
    : 0;
  const headerSystemTime = formatHeaderTime(effectiveServerNowMs);
  const headerUptime = formatHeaderUptime(effectiveServerUptimeMs);

  return (
    <div className="app-shell">
      <div className="app-top-status" aria-label="System clock and uptime">
        <div className="app-top-status-row">
          <span>System Time</span>
          <strong>{headerSystemTime}</strong>
        </div>
        <div className="app-top-status-row">
          <span>Uptime</span>
          <strong>{headerUptime}</strong>
        </div>
      </div>

      <Sidebar activePage={activePage} onNavigate={setActivePage} />

      <main className="main-content">
        {activePage === "overview" ? (
          <OverviewPage onNavigate={setActivePage} />
        ) : activePage === "logs" ? (
          <LogPage />
        ) : activePage === "statistics" ? (
          <StatisticsPage />
        ) : activePage === "backup" ? (
          <BackupPage />
        ) : (
          <ModulePage page={currentPage} alertFeed={alertFeed} />
        )}
      </main>

      <MobileNav activePage={activePage} onNavigate={setActivePage} />
    </div>
  );
}

export default App;
