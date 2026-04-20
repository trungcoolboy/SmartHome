import React from "react";

function ABWorkspaceMap({
  aTravelMm,
  bTravelMm,
  currentAMarker,
  currentBMarker,
  targetAMarker,
  targetBMarker,
  selectedAMarker,
  selectedBMarker,
  onSelect,
}) {
  const mapRef = React.useRef(null);
  const [hoverPoint, setHoverPoint] = React.useState(null);
  const [localSelectedPoint, setLocalSelectedPoint] = React.useState(null);
  const gridColumns = 48;
  const gridRows = 24;

  function resolvePoint(clientX, clientY) {
    const rect = mapRef.current?.getBoundingClientRect();
    if (!rect || rect.width <= 0 || rect.height <= 0) {
      return null;
    }
    if (aTravelMm <= 0 || bTravelMm <= 0) {
      return null;
    }

    const normalizedX = Math.max(0, Math.min(1, (clientX - rect.left) / rect.width));
    const normalizedY = Math.max(0, Math.min(1, (clientY - rect.top) / rect.height));
    const aTargetMm = Math.round(normalizedX * aTravelMm);
    const bTargetMm = Math.round((1 - normalizedY) * bTravelMm);
    return {
      aTargetMm,
      bTargetMm,
      left: normalizedX * 100,
      top: normalizedY * 100,
    };
  }

  function handleSelect(clientX, clientY) {
    const point = resolvePoint(clientX, clientY);
    if (!point) {
      return;
    }
    setLocalSelectedPoint(point);
    onSelect?.({ aTargetMm: point.aTargetMm, bTargetMm: point.bTargetMm });
  }

  function handleHover(clientX, clientY) {
    const point = resolvePoint(clientX, clientY);
    setHoverPoint(point);
  }

  const visibleSelectedLeft = selectedAMarker != null ? selectedAMarker : localSelectedPoint?.left ?? null;
  const visibleSelectedTop = selectedBMarker != null ? 100 - selectedBMarker : localSelectedPoint?.top ?? null;
  const hitCells = React.useMemo(() => {
    const cells = [];
    for (let row = 0; row < gridRows; row += 1) {
      for (let column = 0; column < gridColumns; column += 1) {
        const leftRatio = (column + 0.5) / gridColumns;
        const topRatio = (row + 0.5) / gridRows;
        cells.push({
          key: `${row}-${column}`,
          aTargetMm: Math.round(leftRatio * aTravelMm),
          bTargetMm: Math.round((1 - topRatio) * bTravelMm),
          left: leftRatio * 100,
          top: topRatio * 100,
        });
      }
    }
    return cells;
  }, [aTravelMm, bTravelMm]);

  return (
    <div className="ab-workspace-map-wrap">
      <div
        ref={mapRef}
        className="ab-workspace-map ab-workspace-map-surface"
        role="img"
        aria-label="AB workspace map"
        onPointerMove={(event) => handleHover(event.clientX, event.clientY)}
        onMouseMove={(event) => handleHover(event.clientX, event.clientY)}
        onTouchMove={(event) => {
          const touch = event.touches?.[0];
          if (touch) {
            handleHover(touch.clientX, touch.clientY);
          }
        }}
        onPointerLeave={() => setHoverPoint(null)}
        onMouseLeave={() => setHoverPoint(null)}
        onTouchEnd={() => setHoverPoint(null)}
      >
        <div className="ab-workspace-axis-label ab-workspace-axis-label-top">B</div>
        <div className="ab-workspace-axis-label ab-workspace-axis-label-right">A</div>
        <div
          className="ab-workspace-hit-grid"
          style={{
            gridTemplateColumns: `repeat(${gridColumns}, 1fr)`,
            gridTemplateRows: `repeat(${gridRows}, 1fr)`,
          }}
        >
          {hitCells.map((cell) => (
            <button
              key={cell.key}
              type="button"
              className="ab-workspace-hit-cell"
              aria-label={`Select A ${cell.aTargetMm} mm B ${cell.bTargetMm} mm`}
              title={`A ${cell.aTargetMm} mm / B ${cell.bTargetMm} mm`}
              onMouseEnter={() => setHoverPoint(cell)}
              onFocus={() => setHoverPoint(cell)}
              onClick={() => {
                setLocalSelectedPoint(cell);
                onSelect?.({ aTargetMm: cell.aTargetMm, bTargetMm: cell.bTargetMm });
              }}
            />
          ))}
        </div>
        {hoverPoint ? (
          <div
            className="ab-workspace-hover-tooltip"
            style={{ left: `${hoverPoint.left}%`, top: `${hoverPoint.top}%` }}
          >
            A {hoverPoint.aTargetMm} mm / B {hoverPoint.bTargetMm} mm
          </div>
        ) : null}
        {localSelectedPoint ? (
          <div className="ab-workspace-last-selected">
            Last selected: A {localSelectedPoint.aTargetMm} mm / B {localSelectedPoint.bTargetMm} mm
          </div>
        ) : null}

        <div
          className="ab-workspace-marker ab-workspace-target-marker"
          style={{ left: `${targetAMarker}%`, top: `${100 - targetBMarker}%` }}
        />
        {visibleSelectedLeft != null && visibleSelectedTop != null ? (
          <div
            className="ab-workspace-marker ab-workspace-selected-marker"
            style={{ left: `${visibleSelectedLeft}%`, top: `${visibleSelectedTop}%` }}
          />
        ) : null}
        <div
          className="ab-workspace-marker ab-workspace-current-marker"
          style={{ left: `${currentAMarker}%`, top: `${100 - currentBMarker}%` }}
        />
      </div>
    </div>
  );
}

export default ABWorkspaceMap;
