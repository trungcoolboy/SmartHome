import React, { useEffect, useState } from "react";
import MobileNav from "./components/MobileNav";
import ModulePage from "./components/ModulePage";
import OverviewPage from "./components/OverviewPage";
import Sidebar from "./components/Sidebar";
import { alertFeed, navItems, pageContent } from "./components/mockData";

function getInitialPage() {
  const hashPage = window.location.hash.replace(/^#/, "");
  if (hashPage && navItems.some((item) => item.id === hashPage)) {
    return hashPage;
  }
  return "overview";
}

function App() {
  const [activePage, setActivePage] = useState(getInitialPage);
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

  return (
    <div className="app-shell">
      <Sidebar activePage={activePage} onNavigate={setActivePage} />

      <main className="main-content">
        {activePage === "overview" ? (
          <OverviewPage onNavigate={setActivePage} />
        ) : (
          <ModulePage page={currentPage} alertFeed={alertFeed} />
        )}
      </main>

      <MobileNav activePage={activePage} onNavigate={setActivePage} />
    </div>
  );
}

export default App;
