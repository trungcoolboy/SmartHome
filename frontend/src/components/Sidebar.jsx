import React from "react";
import { navItems } from "./mockData";

function Sidebar({ activePage, onNavigate }) {
  return (
    <aside className="sidebar-card">
      <div className="brand-block">
        <span className="eyebrow">Smart Home + Aqua</span>
        <h1>Control Center</h1>
        <p>Unified control surface for room nodes, water systems and motion hardware.</p>
      </div>

      <div className="sidebar-highlight">
        <span className="panel-label">Mission Focus</span>
        <strong>Fast glance status, then deep technical control.</strong>
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
