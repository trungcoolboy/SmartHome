import React from "react";
import { navItems } from "./mockData";

function MobileNav({ activePage, onNavigate }) {
  return (
    <div className="mobile-nav" aria-label="Mobile navigation">
      {navItems.map((item) => (
        <button
          key={item.id}
          className={item.id === activePage ? "mobile-nav-item active" : "mobile-nav-item"}
          type="button"
          onClick={() => onNavigate(item.id)}
        >
          {item.label}
        </button>
      ))}
    </div>
  );
}

export default MobileNav;
