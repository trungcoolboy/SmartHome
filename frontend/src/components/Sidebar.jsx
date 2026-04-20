import React from "react";
import { navItems } from "./mockData";

function Sidebar({ activePage, onNavigate }) {
  return (
    <aside className="sidebar-card">
      <div className="brand-block">
        <span className="eyebrow">Smart Home</span>
        <h1>Control Center</h1>
      </div>

      <nav className="nav-list" aria-label="Primary">
        {navItems.map((item) => (
          <button
            key={item.id}
            className={item.id === activePage ? "nav-item active" : "nav-item"}
            type="button"
            onClick={() => onNavigate(item.id)}
          >
            {item.label}
          </button>
        ))}
      </nav>

      <div className="system-panel">
        <span className="panel-label">System Status</span>
        <strong>Ubuntu Server Online</strong>
        <p>MQTT, serial bridge and realtime services are centralized here.</p>
      </div>
    </aside>
  );
}

export default Sidebar;
