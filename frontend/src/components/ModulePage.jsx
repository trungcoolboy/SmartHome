import React, { useEffect, useRef, useState } from "react";
import { getApiBaseUrl } from "../api";
import ABWorkspaceMap from "./ABWorkspaceMap";
import youtubeIcon from "../assets/tv-apps/youtube.svg";
import spotifyIcon from "../assets/tv-apps/spotify.svg";
import browserIcon from "../assets/tv-apps/browser.svg";
import fptPlayIcon from "../assets/tv-apps/fptplay.ico";
import vieonIcon from "../assets/tv-apps/vieon.ico";
import vtvGoIcon from "../assets/tv-apps/vtvgo.ico";

const SERVO_DIAL_START_DEG = -135;
const SERVO_DIAL_END_DEG = 135;
const SERVO_DIAL_SWEEP_DEG = SERVO_DIAL_END_DEG - SERVO_DIAL_START_DEG;

function clampServoDialValue(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function servoDialAngleFromValue(value, min, max) {
  if (max <= min) {
    return SERVO_DIAL_START_DEG;
  }
  const ratio = (clampServoDialValue(value, min, max) - min) / (max - min);
  return SERVO_DIAL_START_DEG + ratio * SERVO_DIAL_SWEEP_DEG;
}

function servoDialValueFromPointer(clientX, clientY, bounds, min, max) {
  const centerX = bounds.left + bounds.width / 2;
  const centerY = bounds.top + bounds.height / 2;
  const angleDeg = (Math.atan2(clientY - centerY, clientX - centerX) * 180) / Math.PI;
  const clampedAngle = Math.max(SERVO_DIAL_START_DEG, Math.min(SERVO_DIAL_END_DEG, angleDeg));
  const ratio = (clampedAngle - SERVO_DIAL_START_DEG) / SERVO_DIAL_SWEEP_DEG;
  return Math.round(min + ratio * (max - min));
}

function ServoDial({ min, max, value, disabled, onChange }) {
  const dialRef = useRef(null);
  const draggingRef = useRef(false);
  const angleDeg = servoDialAngleFromValue(value, min, max);
  const angleRad = (angleDeg * Math.PI) / 180;
  const center = 60;
  const needleLength = 34;
  const knobRadius = 7;
  const knobX = center + Math.cos(angleRad) * needleLength;
  const knobY = center + Math.sin(angleRad) * needleLength;

  function updateFromPointer(event) {
    if (!dialRef.current || disabled) {
      return;
    }
    const nextValue = servoDialValueFromPointer(
      event.clientX,
      event.clientY,
      dialRef.current.getBoundingClientRect(),
      min,
      max,
    );
    onChange(nextValue);
  }

  return (
    <div
      ref={dialRef}
      className={`servo-dial ${disabled ? "is-disabled" : ""}`}
      onPointerDown={(event) => {
        if (disabled) {
          return;
        }
        draggingRef.current = true;
        event.currentTarget.setPointerCapture(event.pointerId);
        updateFromPointer(event);
      }}
      onPointerMove={(event) => {
        if (!draggingRef.current) {
          return;
        }
        updateFromPointer(event);
      }}
      onPointerUp={(event) => {
        draggingRef.current = false;
        if (event.currentTarget.hasPointerCapture(event.pointerId)) {
          event.currentTarget.releasePointerCapture(event.pointerId);
        }
      }}
      onPointerCancel={(event) => {
        draggingRef.current = false;
        if (event.currentTarget.hasPointerCapture(event.pointerId)) {
          event.currentTarget.releasePointerCapture(event.pointerId);
        }
      }}
    >
      <svg viewBox="0 0 120 120" aria-hidden="true">
        <circle className="servo-dial-ring" cx="60" cy="60" r="44" />
        <path
          className="servo-dial-arc"
          d="M28.9 91.1A44 44 0 1 1 91.1 91.1"
        />
        <line className="servo-dial-needle" x1={center} y1={center} x2={knobX} y2={knobY} />
        <circle className="servo-dial-hub" cx={center} cy={center} r="5" />
        <circle className="servo-dial-knob" cx={knobX} cy={knobY} r={knobRadius} />
      </svg>
      <div className="servo-dial-value">
        <strong>{value}°</strong>
      </div>
    </div>
  );
}

function ServoTwinDial({
  fan1Min,
  fan1Max,
  fan1Value,
  fan2Min,
  fan2Max,
  fan2Value,
  disabled,
  onChangeFan1,
  onChangeFan2,
  onCommitFan1,
  onCommitFan2,
}) {
  const dialRef = useRef(null);
  const draggingRef = useRef(false);
  const activeNeedleRef = useRef("fan1");
  const fan1ValueRef = useRef(fan1Value);
  const fan2ValueRef = useRef(fan2Value);
  const center = 110;
  const needleLength = 88;
  const FAN1_ARC_START = -155;
  const FAN1_ARC_END = -92;
  const FAN2_ARC_START = -88;
  const FAN2_ARC_END = -25;

  fan1ValueRef.current = fan1Value;
  fan2ValueRef.current = fan2Value;

  function angleFromValue(value, min, max, startDeg, endDeg, reversed = false) {
    if (max <= min) {
      return startDeg;
    }
    const rawRatio = (clampServoDialValue(value, min, max) - min) / (max - min);
    const ratio = reversed ? (1 - rawRatio) : rawRatio;
    return startDeg + ratio * (endDeg - startDeg);
  }

  function valueFromPointer(clientX, clientY, bounds, min, max, startDeg, endDeg, reversed = false) {
    const centerX = bounds.left + bounds.width / 2;
    const centerY = bounds.top + bounds.height / 2;
    const angleDeg = (Math.atan2(clientY - centerY, clientX - centerX) * 180) / Math.PI;
    const clampedAngle = Math.max(Math.min(startDeg, endDeg), Math.min(Math.max(startDeg, endDeg), angleDeg));
    const rawRatio = (clampedAngle - startDeg) / (endDeg - startDeg || 1);
    const ratio = reversed ? (1 - rawRatio) : rawRatio;
    return Math.round(min + ratio * (max - min));
  }

  const fan1AngleDeg = angleFromValue(fan1Value, fan1Min, fan1Max, FAN1_ARC_START, FAN1_ARC_END, true);
  const fan2AngleDeg = angleFromValue(fan2Value, fan2Min, fan2Max, FAN2_ARC_START, FAN2_ARC_END, true);

  function pickActiveNeedle(event, bounds) {
    const localX = event.clientX - bounds.left;
    const localY = event.clientY - bounds.top;
    const fan1Distance = Math.hypot(localX - fan1Needle.x, localY - fan1Needle.y);
    const fan2Distance = Math.hypot(localX - fan2Needle.x, localY - fan2Needle.y);
    const knobThreshold = 24;

    if (fan1Distance <= knobThreshold || fan2Distance <= knobThreshold) {
      return fan1Distance <= fan2Distance ? "fan1" : "fan2";
    }

    return event.clientX < bounds.left + bounds.width / 2 ? "fan1" : "fan2";
  }

  function updateFromPointer(event) {
    if (!dialRef.current || disabled) {
      return;
    }
    const bounds = dialRef.current.getBoundingClientRect();
    const nextNeedle = activeNeedleRef.current;
    if (nextNeedle === "fan1") {
      const nextValue = valueFromPointer(
        event.clientX,
        event.clientY,
        bounds,
        fan1Min,
        fan1Max,
        FAN1_ARC_START,
        FAN1_ARC_END,
        true,
      );
      const clamped = clampServoDialValue(nextValue, fan1Min, fan1Max);
      fan1ValueRef.current = clamped;
      onChangeFan1(clamped);
      return;
    }
    const nextValue = valueFromPointer(
      event.clientX,
      event.clientY,
      bounds,
      fan2Min,
      fan2Max,
      FAN2_ARC_START,
      FAN2_ARC_END,
      true,
    );
    const clamped = clampServoDialValue(nextValue, fan2Min, fan2Max);
    fan2ValueRef.current = clamped;
    onChangeFan2(clamped);
  }

  function commitActiveNeedle() {
    if (activeNeedleRef.current === "fan1") {
      onCommitFan1?.(fan1ValueRef.current);
      return;
    }
    onCommitFan2?.(fan2ValueRef.current);
  }

  function needleEnd(angleDeg) {
    const angleRad = (angleDeg * Math.PI) / 180;
    return {
      x: center + Math.cos(angleRad) * needleLength,
      y: center + Math.sin(angleRad) * needleLength,
    };
  }

  const fan1Needle = needleEnd(fan1AngleDeg);
  const fan2Needle = needleEnd(fan2AngleDeg);

  return (
    <div
      ref={dialRef}
      className={`servo-twin-dial ${disabled ? "is-disabled" : ""}`}
      onPointerDown={(event) => {
        if (disabled) {
          return;
        }
        draggingRef.current = true;
        activeNeedleRef.current = pickActiveNeedle(event, event.currentTarget.getBoundingClientRect());
        event.currentTarget.setPointerCapture(event.pointerId);
        updateFromPointer(event);
      }}
      onPointerMove={(event) => {
        if (!draggingRef.current) {
          return;
        }
        updateFromPointer(event);
      }}
      onPointerUp={(event) => {
        if (draggingRef.current) {
          commitActiveNeedle();
        }
        draggingRef.current = false;
        if (event.currentTarget.hasPointerCapture(event.pointerId)) {
          event.currentTarget.releasePointerCapture(event.pointerId);
        }
      }}
      onPointerCancel={(event) => {
        draggingRef.current = false;
        if (event.currentTarget.hasPointerCapture(event.pointerId)) {
          event.currentTarget.releasePointerCapture(event.pointerId);
        }
      }}
    >
      <svg viewBox="0 0 220 220" aria-hidden="true">
        <path
          className="servo-twin-dial-track"
          d="M47.8 172.2A88 88 0 1 1 172.2 172.2"
        />
        <path
          className="servo-twin-dial-arc"
          d="M47.8 172.2A88 88 0 1 1 172.2 172.2"
        />
        <line className="servo-twin-dial-tick" x1="62" y1="160" x2="72" y2="154" />
        <line className="servo-twin-dial-tick" x1="74" y1="78" x2="82" y2="86" />
        <line className="servo-twin-dial-tick" x1="110" y1="42" x2="110" y2="54" />
        <line className="servo-twin-dial-tick" x1="146" y1="78" x2="138" y2="86" />
        <line className="servo-twin-dial-tick" x1="158" y1="160" x2="148" y2="154" />
        <line className="servo-twin-dial-needle servo-twin-dial-needle-fan1" x1={center} y1={center} x2={fan1Needle.x} y2={fan1Needle.y} />
        <line className="servo-twin-dial-needle servo-twin-dial-needle-fan2" x1={center} y1={center} x2={fan2Needle.x} y2={fan2Needle.y} />
        <circle className="servo-twin-dial-knob servo-twin-dial-knob-fan1" cx={fan1Needle.x} cy={fan1Needle.y} r="9" />
        <circle className="servo-twin-dial-knob servo-twin-dial-knob-fan2" cx={fan2Needle.x} cy={fan2Needle.y} r="8" />
        <circle className="servo-twin-dial-hub" cx={center} cy={center} r="10" />
        <text className="servo-twin-dial-label servo-twin-dial-label-left" x="52" y="186">
          F1
        </text>
        <text className="servo-twin-dial-label servo-twin-dial-label-right" x="168" y="186">
          F2
        </text>
      </svg>
      <div className="servo-twin-dial-values">
        <strong>F1 {fan1Value}°</strong>
        <strong>F2 {fan2Value}°</strong>
      </div>
    </div>
  );
}

function ServoGaugeDial({
  min,
  max,
  value,
  disabled,
  label = "",
  onChange,
  onCommit,
}) {
  const dialRef = useRef(null);
  const draggingRef = useRef(false);
  const center = 110;
  const needleLength = 88;
  const ARC_START = -155;
  const ARC_END = -25;

  function angleFromValue(nextValue, minValue, maxValue, startDeg, endDeg) {
    if (maxValue <= minValue) {
      return startDeg;
    }
    const ratio = (clampServoDialValue(nextValue, minValue, maxValue) - minValue) / (maxValue - minValue);
    return startDeg + ratio * (endDeg - startDeg);
  }

  function valueFromPointer(clientX, clientY, bounds, minValue, maxValue, startDeg, endDeg) {
    const centerX = bounds.left + bounds.width / 2;
    const centerY = bounds.top + bounds.height / 2;
    const angleDeg = (Math.atan2(clientY - centerY, clientX - centerX) * 180) / Math.PI;
    const clampedAngle = Math.max(Math.min(startDeg, endDeg), Math.min(Math.max(startDeg, endDeg), angleDeg));
    const ratio = (clampedAngle - startDeg) / (endDeg - startDeg || 1);
    return Math.round(minValue + ratio * (maxValue - minValue));
  }

  function needleEnd(angleDeg) {
    const angleRad = (angleDeg * Math.PI) / 180;
    return {
      x: center + Math.cos(angleRad) * needleLength,
      y: center + Math.sin(angleRad) * needleLength,
    };
  }

  function updateFromPointer(event) {
    if (!dialRef.current || disabled) {
      return;
    }
    const nextValue = valueFromPointer(
      event.clientX,
      event.clientY,
      dialRef.current.getBoundingClientRect(),
      min,
      max,
      ARC_START,
      ARC_END,
    );
    onChange(clampServoDialValue(nextValue, min, max));
  }

  const angleDeg = angleFromValue(value, min, max, ARC_START, ARC_END);
  const needle = needleEnd(angleDeg);

  return (
    <div
      ref={dialRef}
      className={`servo-twin-dial servo-single-dial ${disabled ? "is-disabled" : ""}`}
      onPointerDown={(event) => {
        if (disabled) {
          return;
        }
        draggingRef.current = true;
        event.currentTarget.setPointerCapture(event.pointerId);
        updateFromPointer(event);
      }}
      onPointerMove={(event) => {
        if (!draggingRef.current) {
          return;
        }
        updateFromPointer(event);
      }}
      onPointerUp={(event) => {
        if (draggingRef.current) {
          onCommit?.(value);
        }
        draggingRef.current = false;
        if (event.currentTarget.hasPointerCapture(event.pointerId)) {
          event.currentTarget.releasePointerCapture(event.pointerId);
        }
      }}
      onPointerCancel={(event) => {
        draggingRef.current = false;
        if (event.currentTarget.hasPointerCapture(event.pointerId)) {
          event.currentTarget.releasePointerCapture(event.pointerId);
        }
      }}
    >
      <svg viewBox="0 0 220 220" aria-hidden="true">
        <path
          className="servo-twin-dial-track"
          d="M47.8 172.2A88 88 0 1 1 172.2 172.2"
        />
        <path
          className="servo-twin-dial-arc"
          d="M47.8 172.2A88 88 0 1 1 172.2 172.2"
        />
        <line className="servo-twin-dial-tick" x1="62" y1="160" x2="72" y2="154" />
        <line className="servo-twin-dial-tick" x1="74" y1="78" x2="82" y2="86" />
        <line className="servo-twin-dial-tick" x1="110" y1="42" x2="110" y2="54" />
        <line className="servo-twin-dial-tick" x1="146" y1="78" x2="138" y2="86" />
        <line className="servo-twin-dial-tick" x1="158" y1="160" x2="148" y2="154" />
        <line className="servo-twin-dial-needle servo-twin-dial-needle-fan1" x1={center} y1={center} x2={needle.x} y2={needle.y} />
        <circle className="servo-twin-dial-knob servo-twin-dial-knob-fan1" cx={needle.x} cy={needle.y} r="9" />
        <circle className="servo-twin-dial-hub" cx={center} cy={center} r="10" />
      </svg>
      <div className="servo-twin-dial-values">
        <strong>{label ? `${label} ${value}°` : `${value}°`}</strong>
      </div>
    </div>
  );
}

function NumberWheelPicker({ label, min, max, value, disabled, orientation = "vertical", onChange, onCommit }) {
  const wheelRef = useRef(null);
  const commitTimerRef = useRef(null);
  const itemExtent = 38;
  const values = Array.from({ length: Math.max(0, max - min + 1) }, (_, index) => min + index);
  const clampedValue = clampServoDialValue(value, min, max);

  useEffect(() => {
    if (!wheelRef.current) {
      return undefined;
    }
    wheelRef.current.scrollTo(
      orientation === "horizontal"
        ? {
            left: (clampedValue - min) * itemExtent,
            behavior: "auto",
          }
        : {
            top: (clampedValue - min) * itemExtent,
            behavior: "auto",
          },
    );

    return () => {
      if (commitTimerRef.current) {
        window.clearTimeout(commitTimerRef.current);
        commitTimerRef.current = null;
      }
    };
  }, [clampedValue, min, orientation]);

  function scheduleCommit(nextValue) {
    if (commitTimerRef.current) {
      window.clearTimeout(commitTimerRef.current);
    }
    commitTimerRef.current = window.setTimeout(() => {
      onCommit?.(nextValue);
    }, 120);
  }

  function handleScroll(event) {
    const nextIndex = Math.round(
      (orientation === "horizontal" ? event.currentTarget.scrollLeft : event.currentTarget.scrollTop) / itemExtent,
    );
    const nextValue = clampServoDialValue(min + nextIndex, min, max);
    onChange(nextValue);
    scheduleCommit(nextValue);
  }

  function handleWheel(event) {
    if (!wheelRef.current || disabled) {
      return;
    }
    event.preventDefault();
    if (orientation === "horizontal") {
      wheelRef.current.scrollLeft += event.deltaY + event.deltaX;
      return;
    }
    wheelRef.current.scrollTop += event.deltaY;
  }

  return (
    <div className={`number-wheel-picker number-wheel-picker-${orientation} ${disabled ? "is-disabled" : ""}`}>
      <span>{label}</span>
      <div className={`number-wheel-shell number-wheel-shell-${orientation}`}>
        <div className="number-wheel-highlight" aria-hidden="true" />
        <div
          ref={wheelRef}
          className={`number-wheel-list number-wheel-list-${orientation}`}
          onScroll={handleScroll}
          onWheel={handleWheel}
        >
          <div className="number-wheel-spacer" aria-hidden="true" />
          {values.map((itemValue) => (
            <button
              key={`${label}-${itemValue}`}
              type="button"
              className={`number-wheel-item ${itemValue === clampedValue ? "is-active" : ""}`}
              disabled={disabled}
              onClick={() => {
                onChange(itemValue);
                scheduleCommit(itemValue);
                wheelRef.current?.scrollTo(
                  orientation === "horizontal"
                    ? {
                        left: (itemValue - min) * itemExtent,
                        behavior: "smooth",
                      }
                    : {
                        top: (itemValue - min) * itemExtent,
                        behavior: "smooth",
                      },
                );
              }}
            >
              {itemValue}
            </button>
          ))}
          <div className="number-wheel-spacer" aria-hidden="true" />
        </div>
      </div>
      <strong>{clampedValue}°</strong>
    </div>
  );
}

function ServoHeadDial({
  pan1Min,
  pan1Max,
  pan1Value,
  pan2Min,
  pan2Max,
  pan2Value,
  disabled,
  onChangePan1,
  onChangePan2,
  onCommitPan1,
  onCommitPan2,
}) {
  const headRef = useRef(null);
  const draggingRef = useRef(false);
  const activePartRef = useRef("pan2");
  const pan1ValueRef = useRef(pan1Value);
  const pan2ValueRef = useRef(pan2Value);

  pan1ValueRef.current = pan1Value;
  pan2ValueRef.current = pan2Value;

  const pan1Ratio = (clampServoDialValue(pan1Value, pan1Min, pan1Max) - pan1Min) / Math.max(1, pan1Max - pan1Min);
  const neckYaw = -1 + pan1Ratio * 2;
  const yawAbs = Math.abs(neckYaw);
  const faceTranslateX = neckYaw * 22;
  const faceScaleX = 1 - yawAbs * 0.36;
  const leftEyeShiftX = neckYaw * 8;
  const rightEyeShiftX = neckYaw * 4;
  const leftEyeRadius = 8 + Math.max(0, neckYaw) * 2 - Math.max(0, -neckYaw) * 2;
  const rightEyeRadius = 8 + Math.max(0, -neckYaw) * 2 - Math.max(0, neckYaw) * 2;
  const glossOpacity = 0.14 + yawAbs * 0.1;
  const pan2Ratio = (clampServoDialValue(pan2Value, pan2Min, pan2Max) - pan2Min) / Math.max(1, pan2Max - pan2Min);
  const facePitch = -1 + pan2Ratio * 2;
  const faceTranslateY = facePitch * -8;
  const faceScaleY = 1 - Math.abs(facePitch) * 0.2;
  const eyeTranslateY = facePitch * -3;
  const mouthTranslateY = facePitch * 4;
  const shellTop = 46;
  const shellBottom = 158;
  const shellHeight = shellBottom - shellTop;
  const shellFrontWidth = 96 * faceScaleX;
  const shellFrontX = 110 - shellFrontWidth / 2 + faceTranslateX;
  const shellDepth = 14 + yawAbs * 26;
  const shellTopLift = 12 + yawAbs * 12;
  const faceplateWidth = shellFrontWidth * 0.67;
  const faceplateHeight = 74 * faceScaleY;
  const faceplateX = 110 - faceplateWidth / 2 + faceTranslateX;
  const faceplateY = 66 + faceTranslateY;
  const leftFaceCenterX = faceplateX + faceplateWidth * 0.28 + leftEyeShiftX;
  const rightFaceCenterX = faceplateX + faceplateWidth * 0.72 + rightEyeShiftX;
  const mouthLeftX = faceplateX + faceplateWidth * 0.24;
  const mouthRightX = faceplateX + faceplateWidth * 0.76;
  const mouthCenterX = faceplateX + faceplateWidth * 0.5;
  const shellRadius = Math.max(18, 28 * faceScaleX);
  const visibleSide = neckYaw >= 0 ? "left" : "right";
  const topOffsetX = neckYaw * 8;
  const topFrontLeftX = shellFrontX;
  const topFrontRightX = shellFrontX + shellFrontWidth;
  const topBackLeftX = shellFrontX - shellDepth + topOffsetX;
  const topBackRightX = shellFrontX + shellFrontWidth + shellDepth + topOffsetX;
  const topFrontY = shellTop;
  const topBackY = shellTop - shellTopLift;
  const visibleSidePoints =
    visibleSide === "left"
      ? `${shellFrontX} ${shellTop + 10}, ${shellFrontX - shellDepth} ${shellTop + 10 - shellTopLift}, ${shellFrontX - shellDepth} ${shellBottom - 10 - shellTopLift}, ${shellFrontX} ${shellBottom - 10}`
      : `${shellFrontX + shellFrontWidth} ${shellTop + 10}, ${shellFrontX + shellFrontWidth + shellDepth} ${shellTop + 10 - shellTopLift}, ${shellFrontX + shellFrontWidth + shellDepth} ${shellBottom - 10 - shellTopLift}, ${shellFrontX + shellFrontWidth} ${shellBottom - 10}`;
  const hiddenSidePoints =
    visibleSide === "left"
      ? `${shellFrontX + shellFrontWidth} ${shellTop + 12}, ${shellFrontX + shellFrontWidth + 5} ${shellTop + 8}, ${shellFrontX + shellFrontWidth + 5} ${shellBottom - 12}, ${shellFrontX + shellFrontWidth} ${shellBottom - 8}`
      : `${shellFrontX} ${shellTop + 12}, ${shellFrontX - 5} ${shellTop + 8}, ${shellFrontX - 5} ${shellBottom - 12}, ${shellFrontX} ${shellBottom - 8}`;

  function valueFromHorizontalPointer(clientX, bounds, min, max) {
    const ratio = Math.max(0, Math.min(1, (clientX - bounds.left) / bounds.width));
    return Math.round(min + ratio * (max - min));
  }

  function updateFromPointer(event) {
    if (!headRef.current || disabled) {
      return;
    }
    const bounds = headRef.current.getBoundingClientRect();
    const nextValue = valueFromHorizontalPointer(event.clientX, bounds, activePartRef.current === "pan1" ? pan1Min : pan2Min, activePartRef.current === "pan1" ? pan1Max : pan2Max);
    if (activePartRef.current === "pan1") {
      const clamped = clampServoDialValue(nextValue, pan1Min, pan1Max);
      pan1ValueRef.current = clamped;
      onChangePan1(clamped);
      return;
    }
    const clamped = clampServoDialValue(nextValue, pan2Min, pan2Max);
    pan2ValueRef.current = clamped;
    onChangePan2(clamped);
  }

  function commitActivePart() {
    if (activePartRef.current === "pan1") {
      onCommitPan1?.(pan1ValueRef.current);
      return;
    }
    onCommitPan2?.(pan2ValueRef.current);
  }

  return (
    <div
      ref={headRef}
      className={`servo-head-dial ${disabled ? "is-disabled" : ""}`}
      onPointerDown={(event) => {
        if (disabled) {
          return;
        }
        const bounds = event.currentTarget.getBoundingClientRect();
        const localY = event.clientY - bounds.top;
        activePartRef.current = localY > bounds.height * 0.72 ? "pan1" : "pan2";
        draggingRef.current = true;
        event.currentTarget.setPointerCapture(event.pointerId);
        updateFromPointer(event);
      }}
      onPointerMove={(event) => {
        if (!draggingRef.current) {
          return;
        }
        updateFromPointer(event);
      }}
      onPointerUp={(event) => {
        if (draggingRef.current) {
          commitActivePart();
        }
        draggingRef.current = false;
        if (event.currentTarget.hasPointerCapture(event.pointerId)) {
          event.currentTarget.releasePointerCapture(event.pointerId);
        }
      }}
      onPointerCancel={(event) => {
        draggingRef.current = false;
        if (event.currentTarget.hasPointerCapture(event.pointerId)) {
          event.currentTarget.releasePointerCapture(event.pointerId);
        }
      }}
    >
      <svg viewBox="0 8 220 188" aria-hidden="true">
        <defs>
          <linearGradient
            id="servoHeadCasingGradient"
            x1={shellFrontX}
            y1={shellTop}
            x2={shellFrontX + shellFrontWidth}
            y2={shellBottom}
            gradientUnits="userSpaceOnUse"
          >
            <stop offset="0%" stopColor="rgba(255,255,255,0.22)" />
            <stop offset="45%" stopColor="rgba(136,160,178,0.14)" />
            <stop offset="100%" stopColor="rgba(20,28,36,0.42)" />
          </linearGradient>
          <linearGradient
            id="servoHeadFaceGradient"
            x1={faceplateX}
            y1={faceplateY}
            x2={faceplateX + faceplateWidth}
            y2={faceplateY + faceplateHeight}
            gradientUnits="userSpaceOnUse"
          >
            <stop offset="0%" stopColor="rgba(154,247,233,0.34)" />
            <stop offset="100%" stopColor="rgba(36,84,97,0.2)" />
          </linearGradient>
          <linearGradient
            id="servoHeadTopGradient"
            x1={topBackLeftX}
            y1={topBackY}
            x2={topFrontRightX}
            y2={topFrontY}
            gradientUnits="userSpaceOnUse"
          >
            <stop offset="0%" stopColor="rgba(255,255,255,0.24)" />
            <stop offset="100%" stopColor="rgba(36,52,68,0.72)" />
          </linearGradient>
        </defs>
        <ellipse className="servo-head-shadow" cx={110 + neckYaw * 12} cy="170" rx={42 - yawAbs * 4} ry="9" />
        <g className="servo-head-shell">
          <polygon
            points={`${topFrontLeftX},${topFrontY} ${topFrontRightX},${topFrontY} ${topBackRightX},${topBackY} ${topBackLeftX},${topBackY}`}
            fill="url(#servoHeadTopGradient)"
            opacity="0.95"
          />
          <polygon points={hiddenSidePoints} className="servo-head-side" style={{ opacity: 0.18 }} />
          <polygon points={visibleSidePoints} className="servo-head-side" style={{ opacity: 0.62 + yawAbs * 0.18 }} />
          <rect x={shellFrontX} y={shellTop} width={shellFrontWidth} height={shellHeight} rx={shellRadius} className="servo-head-casing" />
          <rect x={faceplateX} y={faceplateY} width={faceplateWidth} height={faceplateHeight} rx={20 * faceScaleX} className="servo-head-faceplate" />
          <path d={`M${faceplateX + 8} ${faceplateY + 8}H${faceplateX + faceplateWidth - 8}`} className="servo-head-brow" />
          <path
            d={`M${faceplateX + 6} ${faceplateY + 16}Q${faceplateX + faceplateWidth * 0.5} ${faceplateY - 2} ${faceplateX + faceplateWidth - 6} ${faceplateY + 16}`}
            className="servo-head-gloss"
            style={{ opacity: glossOpacity }}
          />
          <g style={{ transform: `translateY(${eyeTranslateY}px)` }}>
            <ellipse cx={leftFaceCenterX} cy="98" rx={Math.max(5, leftEyeRadius)} ry={Math.max(5.5, leftEyeRadius * 1.05)} className="servo-head-eye" />
            <ellipse cx={rightFaceCenterX} cy="98" rx={Math.max(5, rightEyeRadius)} ry={Math.max(5.5, rightEyeRadius * 1.05)} className="servo-head-eye" />
          </g>
          <path
            d={`M${mouthLeftX} 122Q${mouthCenterX} ${132 + mouthTranslateY} ${mouthRightX} 122`}
            className="servo-head-mouth"
          />
        </g>
      </svg>
    </div>
  );
}

function ModulePage({ page, alertFeed }) {
  const [deviceState, setDeviceState] = useState(null);
  const [deviceError, setDeviceError] = useState("");
  const [deviceAppsError, setDeviceAppsError] = useState("");
  const [deviceAppHistory, setDeviceAppHistory] = useState([]);
  const [deviceAppHistoryError, setDeviceAppHistoryError] = useState("");
  const [deviceAppHistoryNameFilter, setDeviceAppHistoryNameFilter] = useState("");
  const [deviceAppHistoryDateFilter, setDeviceAppHistoryDateFilter] = useState("");
  const [pendingPowerCommand, setPendingPowerCommand] = useState("");
  const [showVolumeHud, setShowVolumeHud] = useState(false);
  const [volumeHudValue, setVolumeHudValue] = useState(0);
  const [volumeHudDragging, setVolumeHudDragging] = useState(false);
  const [bridgeState, setBridgeState] = useState(null);
  const [bridgeLogs, setBridgeLogs] = useState([]);
  const [bridgeError, setBridgeError] = useState("");
  const [bAxisTravelSteps, setBAxisTravelSteps] = useState("");
  const [bAxisDecelWindowSteps, setBAxisDecelWindowSteps] = useState("");
  const [bAxisGotoPosition, setBAxisGotoPosition] = useState("");
  const [bAxisCommandPending, setBAxisCommandPending] = useState(false);
  const [servoStates, setServoStates] = useState({});
  const [byjStates, setByjStates] = useState({});
  const [servoAngleInputs, setServoAngleInputs] = useState({
    fan1: "90",
    fan2: "90",
    pan1: "90",
    pan2: "90",
    lid: "90",
  });
  const [servoControlModes, setServoControlModes] = useState({
    fans: "manual",
    head: "manual",
    lid: "manual",
  });
  const [byjControlModes, setByjControlModes] = useState({
    scraper: "manual",
    feeder: "manual",
  });
  const [byj1TargetMmInput, setByj1TargetMmInput] = useState("0");
  const [byj2StepInput, setByj2StepInput] = useState("5000");
  const [byj2Direction, setByj2Direction] = useState("+");
  const [selectedABTarget, setSelectedABTarget] = useState(null);
  const [selectedATargetMmInput, setSelectedATargetMmInput] = useState("");
  const [selectedBTargetMmInput, setSelectedBTargetMmInput] = useState("");
  const [motionState, setMotionState] = useState({
    a: null,
    b: null,
    ab: null,
  });
  const [roomNodeState, setRoomNodeState] = useState(null);
  const [roomNodeError, setRoomNodeError] = useState("");
  const [roomNodeSecondaryState, setRoomNodeSecondaryState] = useState(null);
  const [roomNodeSecondaryError, setRoomNodeSecondaryError] = useState("");
  const [pumpStates, setPumpStates] = useState({});
  const [miscStates, setMiscStates] = useState({});
  const [sensorStates, setSensorStates] = useState({});
  const [temperatureStates, setTemperatureStates] = useState({});
  const [pendingAquariumControl, setPendingAquariumControl] = useState("");
  const [uploadItems, setUploadItems] = useState([]);
  const [uploadError, setUploadError] = useState("");
  const [uploadInfo, setUploadInfo] = useState("");
  const [uploadingFile, setUploadingFile] = useState(false);
  const [activeModuleTab, setActiveModuleTab] = useState("");
  const [activeRoomDeviceTab, setActiveRoomDeviceTab] = useState("");
  const reconnectTimerRef = useRef(null);
  const statusTimerRef = useRef(null);
  const syncTimerRef = useRef(null);
  const volumeHudTimerRef = useRef(null);
  const volumeHudTrackRef = useRef(null);
  const volumeHudValueRef = useRef(0);
  const volumeHudPointerIdRef = useRef(null);
  const volumeHudDraggingRef = useRef(false);
  const pendingVolumeSyncRef = useRef(false);
  const pendingVolumeTargetRef = useRef(null);
  const bridgeReconnectTimerRef = useRef(null);
  const bridgeStatusTimerRef = useRef(null);
  const featuredDeviceCardRef = useRef(null);
  const roomNodeReconnectTimerRef = useRef(null);
  const roomNodeStatusTimerRef = useRef(null);
  const roomNodeCardRef = useRef(null);
  const roomNodeSecondaryReconnectTimerRef = useRef(null);
  const roomNodeSecondaryStatusTimerRef = useRef(null);
  const roomNodeSecondaryCardRef = useRef(null);
  const uploadInputRef = useRef(null);
  const sectionRefs = useRef({});

  function clampNumber(value, min, max) {
    if (!Number.isFinite(value)) {
      return value;
    }
    return Math.max(min, Math.min(max, value));
  }

  function clampVolume(value) {
    return Math.max(0, Math.min(100, Math.round(value)));
  }

  function formatDateTime(valueSeconds) {
    if (!Number.isFinite(valueSeconds)) {
      return "Unknown";
    }
    return new Date(valueSeconds * 1000).toLocaleString();
  }

  function formatDurationMinutes(valueSeconds) {
    if (!Number.isFinite(valueSeconds)) {
      return "0.0";
    }
    return (valueSeconds / 60).toFixed(1);
  }

  function formatDateForFilter(valueSeconds) {
    if (!Number.isFinite(valueSeconds)) {
      return "";
    }
    const date = new Date(valueSeconds * 1000);
    const year = date.getFullYear();
    const month = String(date.getMonth() + 1).padStart(2, "0");
    const day = String(date.getDate()).padStart(2, "0");
    return `${year}-${month}-${day}`;
  }

  function formatFriendlyError(message, fallback) {
    if (!message) {
      return fallback;
    }
    const normalized = String(message).toLowerCase();
    if (
      normalized.includes("no route to host")
      || normalized.includes("failed to fetch")
      || normalized.includes("networkerror")
      || normalized.includes("network error")
    ) {
      return fallback;
    }
    return message;
  }

  function formatUploadSize(size) {
    if (!Number.isFinite(size)) {
      return "-";
    }
    if (size < 1024) {
      return `${size} B`;
    }
    if (size < 1024 * 1024) {
      return `${(size / 1024).toFixed(1)} KB`;
    }
    return `${(size / (1024 * 1024)).toFixed(2)} MB`;
  }

  function formatUploadModifiedAt(value) {
    if (!Number.isFinite(value)) {
      return "-";
    }
    return new Date(value * 1000).toLocaleString();
  }

  const servoDefinitions = [
    { key: "fan1", label: "Fan 1", min: 0, max: 100 },
    { key: "fan2", label: "Fan 2", min: 70, max: 175 },
    { key: "pan1", label: "Pan 1", min: 0, max: 180 },
    { key: "pan2", label: "Pan 2", min: 50, max: 140 },
    { key: "lid", label: "Lid", min: 0, max: 180 },
  ];
  const byj1MmPerStep = 42 / 50000;
  const groupedWaterSensors = (page.waterLevelSensors?.items ?? []).reduce((groups, item) => {
    const labelParts = String(item.label || "").trim().split(/\s+/);
    const sensorLevel = labelParts.pop() ?? "";
    const sensorName = labelParts.join(" ");
    if (!sensorName || !sensorLevel) {
      return groups;
    }

    const normalizedLevel = sensorLevel.toLowerCase();
    const levelKey =
      normalizedLevel === "low"
        ? "low"
        : normalizedLevel === "normal"
          ? "normal"
          : normalizedLevel === "high"
            ? "high"
            : normalizedLevel;

    const existingGroup = groups.find((group) => group.name === sensorName);
    const levelEntry = {
      id: item.id,
      key: levelKey,
      label: sensorLevel,
      wet: sensorStates[item.id]?.wet ?? item.wet ?? false,
    };

    if (existingGroup) {
      existingGroup.levels[levelKey] = levelEntry;
    } else {
      groups.push({
        name: sensorName,
        levels: {
          low: null,
          normal: null,
          high: null,
          [levelKey]: levelEntry,
        },
      });
    }

    return groups;
  }, []);
  const temperatureReadings = (page.temperatureSensors?.items ?? []).map((item) => {
    const reading = temperatureStates[item.id] ?? {};
    return {
      ...item,
      celsius: Number.isFinite(reading.celsius) ? reading.celsius : item.celsius,
      raw: reading.raw,
    };
  });

  function renderServoGlyph(servoKey) {
    return null;
  }

  function reconcileIncomingDeviceState(nextState) {
    if (!nextState) {
      return nextState;
    }

    const pendingTarget = pendingVolumeTargetRef.current;
    if (pendingTarget == null || nextState.volume == null) {
      return nextState;
    }

    if (clampVolume(nextState.volume) === clampVolume(pendingTarget)) {
      pendingVolumeSyncRef.current = false;
      pendingVolumeTargetRef.current = null;
      return nextState;
    }

    return { ...nextState, volume: pendingTarget };
  }

  function clearScheduledWork() {
    if (reconnectTimerRef.current) {
      window.clearTimeout(reconnectTimerRef.current);
      reconnectTimerRef.current = null;
    }
    if (statusTimerRef.current) {
      window.clearInterval(statusTimerRef.current);
      statusTimerRef.current = null;
    }
    if (syncTimerRef.current) {
      window.clearTimeout(syncTimerRef.current);
      syncTimerRef.current = null;
    }
    if (volumeHudTimerRef.current) {
      window.clearTimeout(volumeHudTimerRef.current);
      volumeHudTimerRef.current = null;
    }
    if (bridgeReconnectTimerRef.current) {
      window.clearTimeout(bridgeReconnectTimerRef.current);
      bridgeReconnectTimerRef.current = null;
    }
    if (bridgeStatusTimerRef.current) {
      window.clearInterval(bridgeStatusTimerRef.current);
      bridgeStatusTimerRef.current = null;
    }
    if (roomNodeReconnectTimerRef.current) {
      window.clearTimeout(roomNodeReconnectTimerRef.current);
      roomNodeReconnectTimerRef.current = null;
    }
    if (roomNodeStatusTimerRef.current) {
      window.clearInterval(roomNodeStatusTimerRef.current);
      roomNodeStatusTimerRef.current = null;
    }
    if (roomNodeSecondaryReconnectTimerRef.current) {
      window.clearTimeout(roomNodeSecondaryReconnectTimerRef.current);
      roomNodeSecondaryReconnectTimerRef.current = null;
    }
    if (roomNodeSecondaryStatusTimerRef.current) {
      window.clearInterval(roomNodeSecondaryStatusTimerRef.current);
      roomNodeSecondaryStatusTimerRef.current = null;
    }
  }

  function getFeaturedBaseUrl() {
    if (!page.featuredDevice?.apiPath) {
      return "";
    }
    return getApiBaseUrl(page.featuredDevice.apiPath);
  }

  function getBridgeBaseUrl() {
    const apiPath = page.bridge?.apiPath ?? page.pumpControl?.apiPath;
    if (!apiPath) {
      return "";
    }
    return getApiBaseUrl(apiPath);
  }

  function getPumpBaseUrl() {
    if (!page.pumpControl?.apiPath) {
      return "";
    }
    return getApiBaseUrl(page.pumpControl.apiPath);
  }

  function getRoomNodeBaseUrl() {
    if (!page.roomNode?.apiPath) {
      return "";
    }
    return getApiBaseUrl(page.roomNode.apiPath);
  }

  function getRoomNodeSecondaryBaseUrl() {
    if (!page.roomNodeSecondary?.apiPath) {
      return "";
    }
    return getApiBaseUrl(page.roomNodeSecondary.apiPath);
  }

  function getUploadBaseUrl() {
    if (!page.uploadPanel?.apiPath) {
      return "";
    }
    return getApiBaseUrl(page.uploadPanel.apiPath);
  }

  function parseBridgeLine(line) {
    if (!line || typeof line !== "string") {
      return null;
    }

    const stateMatch = line.match(/^state\s+(pump|misc)\s+([a-z0-9_]+)\s+mode\s+(auto|manual)\s+output\s+(on|off)$/i);
    if (stateMatch) {
      return {
        kind: "control",
        group: stateMatch[1].toLowerCase(),
        key: stateMatch[2].toLowerCase(),
        mode: stateMatch[3].toLowerCase(),
        state: stateMatch[4].toLowerCase(),
      };
    }

    const sensorMatch = line.match(/^sensor\s+([a-z0-9_]+)\s+(wet|dry)$/i);
    if (sensorMatch) {
      return {
        kind: "sensor",
        key: sensorMatch[1].toLowerCase(),
        wet: sensorMatch[2].toLowerCase() === "wet",
      };
    }

    const tempMatch = line.match(/^temp\s+([a-z0-9_]+)\s+(-?\d+(?:\.\d+)?)\s+raw\s+(\d+)$/i);
    if (tempMatch) {
      return {
        kind: "temperature",
        key: tempMatch[1].toLowerCase(),
        celsius: Number(tempMatch[2]),
        raw: Number(tempMatch[3]),
      };
    }

    return null;
  }

  function parseServoLine(line) {
    if (!line || typeof line !== "string") {
      return null;
    }

    const servoMatch = line.match(/^servo\s+([a-z0-9_]+)\s+us\s+(\d+)\s+angle\s+(\d+)/i);
    if (!servoMatch) {
      return null;
    }

    return {
      key: servoMatch[1].toLowerCase(),
      pulseUs: Number.parseInt(servoMatch[2], 10),
      angle: Number.parseInt(servoMatch[3], 10),
    };
  }

  function parseByjLine(line) {
    if (!line || typeof line !== "string") {
      return null;
    }

    const byjMatch = line.match(
      /^byj\s+(byj[12])\s+enabled\s+(on|off)\s+moving\s+(on|off)\s+pos\s+(-?\d+)\s+target\s+(-?\d+)\s+vel\s+(-?\d+)\s+endstop\s+(trig|clear)(?:\s+mm\s+(-?\d+(?:\.\d+)?))?/i,
    );
    if (!byjMatch) {
      return null;
    }

    return {
      key: byjMatch[1].toLowerCase(),
      enabled: byjMatch[2].toLowerCase() === "on",
      moving: byjMatch[3].toLowerCase() === "on",
      pos: Number.parseInt(byjMatch[4], 10),
      target: Number.parseInt(byjMatch[5], 10),
      vel: Number.parseInt(byjMatch[6], 10),
      endstop: byjMatch[7].toLowerCase(),
      mm: byjMatch[8] != null ? Number.parseFloat(byjMatch[8]) : null,
    };
  }

  function parseMotionLine(line) {
    if (!line || typeof line !== "string") {
      return null;
    }

    if (/STM32G431RB #02 boot/i.test(line)) {
      return {
        kind: "boot_reset",
      };
    }

    const axisMatch = line.match(
      /^axis\s+([ab])\s+enabled\s+(on|off)\s+moving\s+(on|off)\s+pos\s+(-?\d+)\s+target\s+(-?\d+)\s+vel\s+(-?\d+)\s+homed\s+(yes|no)\s+homing\s+([a-z_]+).*?\stravel\s+(\d+)/i,
    );
    if (axisMatch) {
      return {
        kind: "axis",
        axis: axisMatch[1].toLowerCase(),
        enabled: axisMatch[2].toLowerCase() === "on",
        moving: axisMatch[3].toLowerCase() === "on",
        pos: Number.parseInt(axisMatch[4], 10),
        target: Number.parseInt(axisMatch[5], 10),
        vel: Number.parseInt(axisMatch[6], 10),
        homed: axisMatch[7].toLowerCase() === "yes",
        homing: axisMatch[8].toLowerCase(),
        travel: Number.parseInt(axisMatch[9], 10),
      };
    }

    const abMatch = line.match(
      /^ab\s+active\s+(on|off)\s+pos_a\s+(-?\d+)\s+target_a\s+(-?\d+)\s+pos_b\s+(-?\d+)\s+target_b\s+(-?\d+)\s+steps_done\s+(\d+)\s+steps_total\s+(\d+)\s+interval_us\s+(\d+)\s+cruise_us\s+(\d+)\s+start_us\s+(\d+)/i,
    );
    if (abMatch) {
      return {
        kind: "ab",
        active: abMatch[1].toLowerCase() === "on",
        posA: Number.parseInt(abMatch[2], 10),
        targetA: Number.parseInt(abMatch[3], 10),
        posB: Number.parseInt(abMatch[4], 10),
        targetB: Number.parseInt(abMatch[5], 10),
        stepsDone: Number.parseInt(abMatch[6], 10),
        stepsTotal: Number.parseInt(abMatch[7], 10),
        intervalUs: Number.parseInt(abMatch[8], 10),
        cruiseUs: Number.parseInt(abMatch[9], 10),
        startUs: Number.parseInt(abMatch[10], 10),
      };
    }

    const homeMatch = line.match(/^ok axis\s+([ab])\s+homed\s+release_steps\s+(\d+)/i);
    if (homeMatch) {
      return {
        kind: "home_ok",
        axis: homeMatch[1].toLowerCase(),
        releaseSteps: Number.parseInt(homeMatch[2], 10),
      };
    }

    const scanMatch = line.match(/^ok axis\s+([ab])\s+scan\s+travel_steps\s+(\d+)/i);
    if (scanMatch) {
      return {
        kind: "scan_ok",
        axis: scanMatch[1].toLowerCase(),
        travel: Number.parseInt(scanMatch[2], 10),
      };
    }

    const stopMatch = line.match(/^ok axis\s+([ab])\s+stop$/i);
    if (stopMatch) {
      return {
        kind: "stop_ok",
        axis: stopMatch[1].toLowerCase(),
      };
    }

    if (/^ok ab park$/i.test(line)) {
      return {
        kind: "ab_park_ok",
      };
    }

    if (/^ok ab auto_stop$/i.test(line)) {
      return {
        kind: "ab_auto_stop_ok",
      };
    }

    if (/^ok ab stop$/i.test(line)) {
      return {
        kind: "ab_stop_ok",
      };
    }

    const axisMotionMatch = line.match(/^axis\s+([ab])\s+enabled\s+(on|off)\s+moving\s+(on|off).*homing\s+([a-z_]+)/i);
    if (axisMotionMatch) {
      return {
        kind: "axis_motion",
        axis: axisMotionMatch[1].toLowerCase(),
        moving: axisMotionMatch[3].toLowerCase() === "on",
        homingState: axisMotionMatch[4].toLowerCase(),
      };
    }

    return null;
  }

  function applyMotionLinesToState(lines) {
    if (!lines?.length) {
      return;
    }

    setMotionState((current) => {
      let next = current;

      function syncAbStateFromAxes() {
        const nextA = next.a ?? current.a ?? null;
        const nextB = next.b ?? current.b ?? null;
        const existingAb = next.ab ?? current.ab ?? null;
        if (!nextA || !nextB) {
          return;
        }

        const axesSettled =
          !nextA.moving
          && !nextB.moving
          && (nextA.homing ?? "idle") === "idle"
          && (nextB.homing ?? "idle") === "idle";

        if (!axesSettled) {
          return;
        }

        // The bridge status endpoint often exposes only the last axis line after
        // coordinated motion completes, so AB can stay stale-active unless we
        // collapse it once both axes are visibly settled.
        next.ab = {
          active: false,
          posA: nextA.pos ?? 0,
          targetA: nextA.target ?? nextA.pos ?? 0,
          posB: nextB.pos ?? 0,
          targetB: nextB.target ?? nextB.pos ?? 0,
          stepsDone: 0,
          stepsTotal: 0,
          intervalUs: 0,
          cruiseUs: 0,
          startUs: 0,
          parked: existingAb?.parked ?? false,
        };
      }

      lines.forEach((line) => {
        const parsed = parseMotionLine(line);
        if (!parsed) {
          return;
        }

        if (next === current) {
          next = { ...current };
        }

        if (parsed.kind === "boot_reset") {
          next = {
            a: null,
            b: null,
            ab: null,
          };
          return;
        }

        if (parsed.kind === "axis") {
          next[parsed.axis] = parsed;
          syncAbStateFromAxes();
          return;
        }

        if (parsed.kind === "ab") {
          next.ab = {
            ...parsed,
            parked:
              parsed.active
                ? false
                : ((next.ab ?? current.ab)?.parked ?? false),
          };
          if (!parsed.active) {
            if (next.a ?? current.a) {
              next.a = { ...(next.a ?? current.a), moving: false };
            }
            if (next.b ?? current.b) {
              next.b = { ...(next.b ?? current.b), moving: false };
            }
          }
          return;
        }

        if (parsed.kind === "home_ok") {
          const existingAxis = next[parsed.axis] ?? current[parsed.axis] ?? {};
          next[parsed.axis] = {
            ...existingAxis,
            enabled: true,
            moving: false,
            homing: "idle",
            homed: true,
            pos: 0,
            target: 0,
            vel: 0,
          };
          next.ab = {
            active: false,
            posA: next.a?.pos ?? current.a?.pos ?? 0,
            targetA: next.a?.target ?? current.a?.target ?? 0,
            posB: next.b?.pos ?? current.b?.pos ?? 0,
            targetB: next.b?.target ?? current.b?.target ?? 0,
            stepsDone: 0,
            stepsTotal: 0,
            intervalUs: 0,
            cruiseUs: 0,
            startUs: 0,
            parked: false,
          };
          return;
        }

        if (parsed.kind === "scan_ok") {
          const existingAxis = next[parsed.axis] ?? current[parsed.axis] ?? {};
          next[parsed.axis] = {
            ...existingAxis,
            enabled: true,
            moving: false,
            homing: "idle",
            homed: true,
            pos: 0,
            target: 0,
            vel: 0,
            travel: parsed.travel,
          };
          next.ab = {
            active: false,
            posA: next.a?.pos ?? current.a?.pos ?? 0,
            targetA: next.a?.target ?? current.a?.target ?? 0,
            posB: next.b?.pos ?? current.b?.pos ?? 0,
            targetB: next.b?.target ?? current.b?.target ?? 0,
            stepsDone: 0,
            stepsTotal: 0,
            intervalUs: 0,
            cruiseUs: 0,
            startUs: 0,
            parked: false,
          };
          return;
        }

        if (parsed.kind === "stop_ok") {
          const existingAxis = next[parsed.axis] ?? current[parsed.axis] ?? {};
          next[parsed.axis] = {
            ...existingAxis,
            enabled: false,
            moving: false,
            homing: "idle",
            homed: false,
            vel: 0,
          };
          next.ab = {
            active: false,
            posA: next.a?.pos ?? current.a?.pos ?? 0,
            targetA: next.a?.target ?? current.a?.target ?? 0,
            posB: next.b?.pos ?? current.b?.pos ?? 0,
            targetB: next.b?.target ?? current.b?.target ?? 0,
            stepsDone: 0,
            stepsTotal: 0,
            intervalUs: 0,
            cruiseUs: 0,
            startUs: 0,
            parked: false,
          };
          return;
        }

        if (parsed.kind === "ab_park_ok") {
          const existingAb = next.ab ?? current.ab ?? {};
          next.ab = {
            ...existingAb,
            parked: true,
          };
          return;
        }

        if (parsed.kind === "ab_auto_stop_ok" || parsed.kind === "ab_stop_ok") {
          const existingAb = next.ab ?? current.ab ?? {};
          next.ab = {
            ...existingAb,
            active: false,
            parked: false,
          };
          return;
        }

        if (parsed.kind === "axis_motion") {
          const existingAxis = next[parsed.axis] ?? current[parsed.axis] ?? {};
          next[parsed.axis] = {
            ...existingAxis,
            moving: parsed.moving,
            homing: parsed.homingState,
          };
          syncAbStateFromAxes();
          return;
        }
      });

      return next;
    });
  }

  function applyBridgeLinesToControls(lines) {
    if (!lines?.length) {
      return;
    }

    const pumpByKey = Object.fromEntries((page.pumpControl?.items ?? []).map((item) => [item.key, item.id]));
    const miscByKey = Object.fromEntries((page.miscControl?.items ?? []).map((item) => [item.key, item.id]));
    const sensorByKey = Object.fromEntries(
      (page.waterLevelSensors?.items ?? []).map((item) => [item.id.replace(/-/g, "_"), item.id]),
    );
    const temperatureByKey = Object.fromEntries(
      (page.temperatureSensors?.items ?? []).map((item) => [item.key ?? item.id.replace(/-/g, "_"), item.id]),
    );

    const nextPumps = {};
    const nextMisc = {};
    const nextSensors = {};
    const nextTemperatures = {};
    const nextServos = {};
    const nextByj = {};

    lines.forEach((line) => {
      const servoParsed = parseServoLine(line);
      if (servoParsed) {
        nextServos[servoParsed.key] = {
          pulseUs: servoParsed.pulseUs,
          angle: servoParsed.angle,
        };
      }

      const byjParsed = parseByjLine(line);
      if (byjParsed) {
        nextByj[byjParsed.key] = byjParsed;
      }

      const parsed = parseBridgeLine(line);
      if (!parsed) {
        return;
      }

      if (parsed.kind === "control") {
        const targetId = parsed.group === "pump" ? pumpByKey[parsed.key] : miscByKey[parsed.key];
        if (!targetId) {
          return;
        }
        const target = parsed.group === "pump" ? nextPumps : nextMisc;
        target[targetId] = {
          mode: parsed.mode,
          state: parsed.state,
        };
        return;
      }

      if (parsed.kind === "temperature") {
        const temperatureId = temperatureByKey[parsed.key];
        if (temperatureId) {
          nextTemperatures[temperatureId] = {
            celsius: parsed.celsius,
            raw: parsed.raw,
          };
        }
        return;
      }

      const sensorId = sensorByKey[parsed.key];
      if (sensorId) {
        nextSensors[sensorId] = { wet: parsed.wet };
      }
    });

    if (Object.keys(nextPumps).length) {
      setPumpStates((current) => ({ ...current, ...nextPumps }));
    }
    if (Object.keys(nextMisc).length) {
      setMiscStates((current) => ({ ...current, ...nextMisc }));
    }
    if (Object.keys(nextSensors).length) {
      setSensorStates((current) => ({ ...current, ...nextSensors }));
    }
    if (Object.keys(nextTemperatures).length) {
      setTemperatureStates((current) => ({ ...current, ...nextTemperatures }));
    }
    if (Object.keys(nextServos).length) {
      setServoStates((current) => ({ ...current, ...nextServos }));
    }
    if (Object.keys(nextByj).length) {
      setByjStates((current) => ({ ...current, ...nextByj }));
    }
  }

  function applyBridgeSnapshotToControls(snapshot) {
    if (!snapshot) {
      return;
    }

    const pumpByKey = Object.fromEntries((page.pumpControl?.items ?? []).map((item) => [item.key, item.id]));
    const miscByKey = Object.fromEntries((page.miscControl?.items ?? []).map((item) => [item.key, item.id]));
    const sensorByKey = Object.fromEntries(
      (page.waterLevelSensors?.items ?? []).map((item) => [item.id.replace(/-/g, "_"), item.id]),
    );
    const temperatureByKey = Object.fromEntries(
      (page.temperatureSensors?.items ?? []).map((item) => [item.key ?? item.id.replace(/-/g, "_"), item.id]),
    );

    const nextPumps = {};
    const nextMisc = {};
    const nextSensors = {};
    const nextTemperatures = {};

    Object.values(snapshot.controls ?? {}).forEach((control) => {
      const targetId = control.group === "pump" ? pumpByKey[control.key] : miscByKey[control.key];
      if (!targetId) {
        return;
      }
      const target = control.group === "pump" ? nextPumps : nextMisc;
      target[targetId] = {
        mode: control.mode,
        state: control.state,
      };
    });

    Object.entries(snapshot.sensors ?? {}).forEach(([key, wet]) => {
      const sensorId = sensorByKey[key];
      if (sensorId) {
        nextSensors[sensorId] = { wet: Boolean(wet) };
      }
    });

    Object.entries(snapshot.temperatures ?? {}).forEach(([key, reading]) => {
      const temperatureId = temperatureByKey[key];
      if (!temperatureId) {
        return;
      }
      nextTemperatures[temperatureId] = {
        celsius: Number(reading?.celsius),
        raw: Number(reading?.raw),
      };
    });

    if (Object.keys(nextPumps).length) {
      setPumpStates((current) => ({ ...current, ...nextPumps }));
    }
    if (Object.keys(nextMisc).length) {
      setMiscStates((current) => ({ ...current, ...nextMisc }));
    }
    if (Object.keys(nextSensors).length) {
      setSensorStates((current) => ({ ...current, ...nextSensors }));
    }
    if (Object.keys(nextTemperatures).length) {
      setTemperatureStates((current) => ({ ...current, ...nextTemperatures }));
    }
  }

  function applyOptimisticCommand(commandPayload) {
    setDeviceState((current) => {
      if (!current) {
        return current;
      }

      if (commandPayload.command === "volume_up") {
        return { ...current, volume: Math.min((current.volume ?? 0) + 1, 100) };
      }
      if (commandPayload.command === "volume_down") {
        return { ...current, volume: Math.max((current.volume ?? 0) - 1, 0) };
      }
      if (commandPayload.command === "set_volume") {
        return { ...current, volume: clampVolume(commandPayload.volume ?? current.volume ?? 0) };
      }
      if (commandPayload.command === "toggle_mute") {
        return { ...current, muted: !current.muted };
      }
      if (commandPayload.command === "turn_off") {
        return current;
      }
      if (commandPayload.command === "turn_on") {
        return { ...current, lastError: null };
      }
      if (commandPayload.command === "set_input" && commandPayload.appId) {
        return { ...current, foregroundAppId: commandPayload.appId };
      }
      return current;
    });
  }

  const liveVolume = deviceState?.volume ?? page.featuredDevice?.volume?.level;

  useEffect(() => {
    if (!page.pumpControl?.items) {
      setPumpStates({});
    } else {
      setPumpStates(
        Object.fromEntries(
          page.pumpControl.items.map((item) => [
            item.id,
            {
              mode: item.mode ?? "auto",
              state: item.state ?? "off",
            },
          ]),
        ),
      );
    }

    if (!page.miscControl?.items) {
      setMiscStates({});
    } else {
      setMiscStates(
        Object.fromEntries(
          page.miscControl.items.map((item) => [
            item.id,
            {
              mode: item.mode ?? "auto",
              state: item.state ?? "off",
            },
          ]),
        ),
      );
    }

    if (!page.waterLevelSensors?.items) {
      setSensorStates({});
    } else {
      setSensorStates(
        Object.fromEntries(
          page.waterLevelSensors.items.map((item) => [
          item.id,
          {
              wet: false,
          },
        ]),
        ),
      );
    }

    if (!page.temperatureSensors?.items) {
      setTemperatureStates({});
    } else {
      setTemperatureStates(
        Object.fromEntries(
          page.temperatureSensors.items.map((item) => [
            item.id,
            {
              celsius: item.celsius ?? null,
              raw: null,
            },
          ]),
        ),
      );
    }
  }, [page]);

  useEffect(() => {
    if (!Object.keys(servoStates).length) {
      return;
    }

    setServoAngleInputs((current) => {
      const next = { ...current };
      let changed = false;

      Object.entries(servoStates).forEach(([key, state]) => {
        if (!state || !Number.isFinite(state.angle)) {
          return;
        }
        const nextValue = String(state.angle);
        if (next[key] !== nextValue) {
          next[key] = nextValue;
          changed = true;
        }
      });

      return changed ? next : current;
    });
  }, [servoStates]);

  useEffect(() => {
    if (page.featuredDevice || !page.deviceStrip?.length) {
      setActiveModuleTab("");
      return;
    }
    setActiveModuleTab(page.deviceStrip[0].id);
  }, [page]);

  useEffect(() => {
    if (!page.featuredDevice) {
      setActiveRoomDeviceTab("");
      return;
    }
    setActiveRoomDeviceTab("featured");
  }, [page]);

  useEffect(() => {
    if (!page.featuredDevice?.apiPath) {
      clearScheduledWork();
      setDeviceState(null);
      setDeviceError("");
      setDeviceAppsError("");
      setDeviceAppHistory([]);
      setDeviceAppHistoryError("");
      setPendingPowerCommand("");
      return;
    }

    let cancelled = false;
    let eventSource = null;
    const baseUrl = getFeaturedBaseUrl();

    async function loadStatus(options = {}) {
      const preserveError = options.preserveError === true;
      try {
        const response = await fetch(`${baseUrl}/status`);
        if (!response.ok) {
          throw new Error(`status ${response.status}`);
        }
        const payload = await response.json();
        if (!cancelled) {
          setDeviceState(reconcileIncomingDeviceState(payload));
          if (!preserveError) {
            setDeviceError("");
          }
        }
      } catch (error) {
        if (!cancelled) {
          setDeviceError(error.message);
        }
      }
    }

    function connectEvents() {
      if (cancelled) {
        return;
      }

      eventSource = new EventSource(`${baseUrl}/events`);
      eventSource.onmessage = (event) => {
        try {
          const payload = JSON.parse(event.data);
          if (!cancelled && payload.type === "snapshot" && payload.state) {
            setDeviceState(reconcileIncomingDeviceState(payload.state));
            setDeviceError("");
          }
        } catch (error) {
          if (!cancelled) {
            setDeviceError(error.message);
          }
        }
      };

      eventSource.onerror = () => {
        if (!cancelled) {
          setDeviceError("realtime stream disconnected");
          eventSource.close();
          reconnectTimerRef.current = window.setTimeout(() => {
            loadStatus({ preserveError: true });
            connectEvents();
          }, 2000);
        }
      };
    }

    loadStatus();
    refreshFeaturedApps();
    refreshFeaturedAppHistory();
    connectEvents();
    statusTimerRef.current = window.setInterval(() => {
      loadStatus({ preserveError: true });
      refreshFeaturedAppHistory();
    }, 15000);

    return () => {
      cancelled = true;
      if (eventSource) {
        eventSource.close();
      }
      clearScheduledWork();
    };
  }, [page.featuredDevice, page.featuredDevice?.apiPath]);

  useEffect(() => {
    if (!pendingPowerCommand || !deviceState) {
      return;
    }

    if (pendingPowerCommand === "turn_on" && deviceState.reachable) {
      setPendingPowerCommand("");
    }

    if (pendingPowerCommand === "turn_off" && !deviceState.reachable) {
      setPendingPowerCommand("");
    }
  }, [deviceState, pendingPowerCommand]);

  useEffect(() => {
    volumeHudValueRef.current = volumeHudValue;
  }, [volumeHudValue]);

  useEffect(() => {
    if (volumeHudDraggingRef.current || pendingVolumeSyncRef.current) {
      return;
    }
    setVolumeHudValue(clampVolume(liveVolume ?? 0));
  }, [liveVolume, volumeHudDragging]);

  useEffect(() => {
    const tuning = page.bridge?.bAxisTuning;
    if (!tuning) {
      setBAxisTravelSteps("");
      setBAxisDecelWindowSteps("");
      setBAxisGotoPosition("");
      return;
    }
    setBAxisTravelSteps(String(tuning.defaultTravelSteps ?? ""));
    setBAxisDecelWindowSteps(String(tuning.defaultDecelWindowSteps ?? ""));
    setBAxisGotoPosition("");
    setMotionState((current) => {
      if (current?.a || current?.b || current?.ab) {
        return current;
      }
      return {
        a: tuning.defaultATravelSteps
          ? {
              enabled: false,
              moving: false,
              pos: 0,
              target: 0,
              vel: 0,
              homed: false,
              travel: tuning.defaultATravelSteps,
            }
          : null,
        b: tuning.defaultTravelSteps
          ? {
              enabled: false,
              moving: false,
              pos: 0,
              target: 0,
              vel: 0,
              homed: false,
              travel: tuning.defaultTravelSteps,
            }
          : null,
        ab: null,
      };
    });
  }, [page.bridge?.bAxisTuning]);

  useEffect(() => {
    const baseUrl = getBridgeBaseUrl();
    if (!baseUrl) {
      setBridgeState(null);
      setBridgeLogs([]);
      setBridgeError("");
      return;
    }

    let cancelled = false;
    let eventSource = null;
    let bridgeStreamConnected = false;
    let bridgeStatusInFlight = false;
    let bridgeLogsInFlight = false;

    async function loadBridgeLogs() {
      if (bridgeLogsInFlight || bridgeStreamConnected) {
        return;
      }
      bridgeLogsInFlight = true;
      try {
        const response = await fetch(`${baseUrl}/logs?limit=40`);
        if (!response.ok) {
          throw new Error(`logs ${response.status}`);
        }
        const payload = await response.json();
        if (!cancelled) {
          const items = payload.items ?? [];
          setBridgeLogs(items.slice(-6));
          applyBridgeLinesToControls(items.map((item) => item.payload));
          applyMotionLinesToState(items.map((item) => item.payload));
        }
      } catch (error) {
        if (!cancelled) {
          setBridgeError(error.message);
        }
      } finally {
        bridgeLogsInFlight = false;
      }
    }

    async function loadBridgeStatus(options = {}) {
      const preserveError = options.preserveError === true;
      if (bridgeStatusInFlight) {
        return;
      }
      bridgeStatusInFlight = true;
      try {
        const response = await fetch(`${baseUrl}/status`);
        if (!response.ok) {
          throw new Error(`status ${response.status}`);
        }
        const payload = await response.json();
        if (!cancelled) {
          setBridgeState(payload);
          applyBridgeSnapshotToControls(payload);
          applyMotionLinesToState([payload.lastLine].filter(Boolean));
          if (!preserveError) {
            setBridgeError("");
          }
        }
      } catch (error) {
        if (!cancelled) {
          setBridgeError(error.message);
        }
      } finally {
        bridgeStatusInFlight = false;
      }
    }

    function connectBridgeEvents() {
      if (cancelled) {
        return;
      }

      if (eventSource) {
        eventSource.close();
        eventSource = null;
      }

      eventSource = new EventSource(`${baseUrl}/events`);
      eventSource.onopen = () => {
        if (!cancelled) {
          bridgeStreamConnected = true;
          setBridgeError("");
        }
      };
      eventSource.onmessage = (event) => {
        try {
          const payload = JSON.parse(event.data);
          if (cancelled) {
            return;
          }
          if (payload.type === "snapshot" && payload.state) {
            setBridgeState(payload.state);
            applyBridgeSnapshotToControls(payload.state);
            applyMotionLinesToState([payload.state.lastLine].filter(Boolean));
            setBridgeError("");
          }
          if ((payload.type === "rx" || payload.type === "tx") && payload.payload) {
            if (payload.type === "rx") {
              applyBridgeLinesToControls([payload.payload]);
              applyMotionLinesToState([payload.payload]);
            }
            setBridgeLogs((current) => {
              const next = [
                ...current,
                {
                  ts: payload.ts ?? Date.now() / 1000,
                  direction: payload.type,
                  payload: payload.payload,
                },
              ];
              return next.slice(-6);
            });
          }
        } catch (error) {
          if (!cancelled) {
            setBridgeError(error.message);
          }
        }
      };

      eventSource.onerror = () => {
        if (!cancelled) {
          bridgeStreamConnected = false;
          setBridgeError("bridge realtime stream disconnected");
          eventSource.close();
          eventSource = null;
          bridgeReconnectTimerRef.current = window.setTimeout(() => {
            loadBridgeStatus({ preserveError: true });
            loadBridgeLogs();
            connectBridgeEvents();
          }, 2000);
        }
      };
    }

    loadBridgeStatus();
    loadBridgeLogs();
    connectBridgeEvents();
    bridgeStatusTimerRef.current = window.setInterval(() => {
      loadBridgeStatus({ preserveError: true });
      if (!bridgeStreamConnected) {
        loadBridgeLogs();
      }
    }, 2000);

    return () => {
      cancelled = true;
      if (eventSource) {
        eventSource.close();
        eventSource = null;
      }
      if (bridgeReconnectTimerRef.current) {
        window.clearTimeout(bridgeReconnectTimerRef.current);
        bridgeReconnectTimerRef.current = null;
      }
      if (bridgeStatusTimerRef.current) {
        window.clearInterval(bridgeStatusTimerRef.current);
        bridgeStatusTimerRef.current = null;
      }
    };
  }, [page.bridge?.apiPath, page.pumpControl?.apiPath]);

  useEffect(() => {
    const pumpApiPath = page.pumpControl?.apiPath;
    if (!pumpApiPath || pumpApiPath === page.bridge?.apiPath) {
      return;
    }

    const baseUrl = getPumpBaseUrl();
    let cancelled = false;
    let eventSource = null;
    let statusInFlight = false;

    async function loadPumpStatus(options = {}) {
      const preserveError = options.preserveError === true;
      if (statusInFlight) {
        return;
      }
      statusInFlight = true;
      try {
        const response = await fetch(`${baseUrl}/status`);
        if (!response.ok) {
          throw new Error(`status ${response.status}`);
        }
        const payload = await response.json();
        if (!cancelled) {
          applyBridgeSnapshotToControls(payload);
          if (!preserveError) {
            setBridgeError("");
          }
        }
      } catch (error) {
        if (!cancelled) {
          setBridgeError(error.message);
        }
      } finally {
        statusInFlight = false;
      }
    }

    function connectPumpEvents() {
      if (cancelled) {
        return;
      }

      if (eventSource) {
        eventSource.close();
        eventSource = null;
      }

      eventSource = new EventSource(`${baseUrl}/events`);
      eventSource.onmessage = (event) => {
        try {
          const payload = JSON.parse(event.data);
          if (cancelled) {
            return;
          }
          if (payload.type === "snapshot" && payload.state) {
            applyBridgeSnapshotToControls(payload.state);
            setBridgeError("");
          }
          if (payload.type === "rx" && payload.payload) {
            applyBridgeLinesToControls([payload.payload]);
          }
        } catch (error) {
          if (!cancelled) {
            setBridgeError(error.message);
          }
        }
      };
      eventSource.onerror = () => {
        if (!cancelled) {
          eventSource.close();
          eventSource = null;
          window.setTimeout(() => {
            loadPumpStatus({ preserveError: true });
            connectPumpEvents();
          }, 2000);
        }
      };
    }

    loadPumpStatus();
    connectPumpEvents();
    const timer = window.setInterval(() => {
      loadPumpStatus({ preserveError: true });
    }, 2000);

    return () => {
      cancelled = true;
      if (eventSource) {
        eventSource.close();
      }
      window.clearInterval(timer);
    };
  }, [page.pumpControl?.apiPath, page.bridge?.apiPath]);

  useEffect(() => {
    const baseUrl = getRoomNodeBaseUrl();
    if (!baseUrl) {
      setRoomNodeState(null);
      setRoomNodeError("");
      return;
    }

    let cancelled = false;
    let eventSource = null;

    async function loadRoomNodeStatus(options = {}) {
      const preserveError = options.preserveError === true;
      try {
        const response = await fetch(`${baseUrl}/status`);
        if (!response.ok) {
          throw new Error(`status ${response.status}`);
        }
        const payload = await response.json();
        if (!cancelled) {
          setRoomNodeState(payload);
          if (!preserveError) {
            setRoomNodeError("");
          }
        }
      } catch (error) {
        if (!cancelled) {
          setRoomNodeError(error.message);
        }
      }
    }

    function connectRoomNodeEvents() {
      if (cancelled) {
        return;
      }

      eventSource = new EventSource(`${baseUrl}/events`);
      eventSource.onmessage = (event) => {
        try {
          const payload = JSON.parse(event.data);
          if (!cancelled && payload.type === "snapshot" && payload.state) {
            setRoomNodeState(payload.state);
            setRoomNodeError("");
          }
        } catch (error) {
          if (!cancelled) {
            setRoomNodeError(error.message);
          }
        }
      };

      eventSource.onerror = () => {
        if (!cancelled) {
          setRoomNodeError("node realtime stream disconnected");
          eventSource.close();
          roomNodeReconnectTimerRef.current = window.setTimeout(() => {
            loadRoomNodeStatus({ preserveError: true });
            connectRoomNodeEvents();
          }, 2000);
        }
      };
    }

    loadRoomNodeStatus();
    connectRoomNodeEvents();
    roomNodeStatusTimerRef.current = window.setInterval(() => {
      loadRoomNodeStatus({ preserveError: true });
    }, 15000);

    return () => {
      cancelled = true;
      if (eventSource) {
        eventSource.close();
      }
      if (roomNodeReconnectTimerRef.current) {
        window.clearTimeout(roomNodeReconnectTimerRef.current);
        roomNodeReconnectTimerRef.current = null;
      }
      if (roomNodeStatusTimerRef.current) {
        window.clearInterval(roomNodeStatusTimerRef.current);
        roomNodeStatusTimerRef.current = null;
      }
    };
  }, [page.roomNode?.apiPath]);

  useEffect(() => {
    const baseUrl = getRoomNodeSecondaryBaseUrl();
    if (!baseUrl) {
      setRoomNodeSecondaryState(null);
      setRoomNodeSecondaryError("");
      return;
    }

    let cancelled = false;
    let eventSource = null;

    async function loadRoomNodeStatus(options = {}) {
      const preserveError = options.preserveError === true;
      try {
        const response = await fetch(`${baseUrl}/status`);
        if (!response.ok) {
          throw new Error(`status ${response.status}`);
        }
        const payload = await response.json();
        if (!cancelled) {
          setRoomNodeSecondaryState(payload);
          if (!preserveError) {
            setRoomNodeSecondaryError("");
          }
        }
      } catch (error) {
        if (!cancelled) {
          setRoomNodeSecondaryError(error.message);
        }
      }
    }

    function connectRoomNodeEvents() {
      if (cancelled) {
        return;
      }

      eventSource = new EventSource(`${baseUrl}/events`);
      eventSource.onmessage = (event) => {
        try {
          const payload = JSON.parse(event.data);
          if (!cancelled && payload.type === "snapshot" && payload.state) {
            setRoomNodeSecondaryState(payload.state);
            setRoomNodeSecondaryError("");
          }
        } catch (error) {
          if (!cancelled) {
            setRoomNodeSecondaryError(error.message);
          }
        }
      };

      eventSource.onerror = () => {
        if (!cancelled) {
          setRoomNodeSecondaryError("node realtime stream disconnected");
          eventSource.close();
          roomNodeSecondaryReconnectTimerRef.current = window.setTimeout(() => {
            loadRoomNodeStatus({ preserveError: true });
            connectRoomNodeEvents();
          }, 2000);
        }
      };
    }

    loadRoomNodeStatus();
    connectRoomNodeEvents();
    roomNodeSecondaryStatusTimerRef.current = window.setInterval(() => {
      loadRoomNodeStatus({ preserveError: true });
    }, 15000);

    return () => {
      cancelled = true;
      if (eventSource) {
        eventSource.close();
      }
      if (roomNodeSecondaryReconnectTimerRef.current) {
        window.clearTimeout(roomNodeSecondaryReconnectTimerRef.current);
        roomNodeSecondaryReconnectTimerRef.current = null;
      }
      if (roomNodeSecondaryStatusTimerRef.current) {
        window.clearInterval(roomNodeSecondaryStatusTimerRef.current);
        roomNodeSecondaryStatusTimerRef.current = null;
      }
    };
  }, [page.roomNodeSecondary?.apiPath]);

  useEffect(() => {
    const baseUrl = getUploadBaseUrl();
    if (!baseUrl) {
      setUploadItems([]);
      setUploadError("");
      setUploadInfo("");
      return;
    }

    let cancelled = false;

    async function loadUploads(preserveInfo = false) {
      try {
        const response = await fetch(baseUrl);
        const payload = await response.json();
        if (!response.ok) {
          throw new Error(payload.error || `uploads ${response.status}`);
        }
        if (!cancelled) {
          setUploadItems(payload.items ?? []);
          setUploadError("");
          if (!preserveInfo) {
            setUploadInfo("");
          }
        }
      } catch (error) {
        if (!cancelled) {
          setUploadError(error.message);
        }
      }
    }

    loadUploads(true);
    return () => {
      cancelled = true;
    };
  }, [page.uploadPanel?.apiPath]);

  const liveConnectivity = deviceState
    ? deviceState.paired
      ? deviceState.wakePending
        ? "Wake pending"
        : deviceState.reachable
          ? deviceState.stale
            ? "Live state is stale"
            : "Paired and live"
          : "Offline"
      : deviceState.reachable
        ? "Reachable, not paired"
        : "Offline"
    : page.featuredDevice?.connectivity;
  const canPowerOn = deviceState ? !deviceState.reachable : false;
  const liveNowPlaying = deviceState?.foregroundAppId ?? page.featuredDevice?.nowPlaying?.title;
  const liveMute = deviceState?.muted;
  const liveTransport = deviceState?.reachable
    ? deviceState.paired
      ? "LAN / paired"
      : "LAN / reachable"
    : page.featuredDevice?.facts?.[2]?.value;
  const liveDetailText = deviceError
    ? formatFriendlyError(deviceError, "TV is offline or not reachable right now.")
    : deviceState?.wakePending
      ? "Wake-on-LAN sent. Waiting for TV network stack and webOS session to come back."
      : deviceState?.lastSeen
      ? `Last sync ${new Date(deviceState.lastSeen * 1000).toLocaleTimeString()}${deviceState.stale ? " (stale)" : ""}`
      : page.featuredDevice?.nowPlaying?.detail;
  const liveSubtitleText = deviceState?.reachable
    ? deviceState.paired
      ? "Realtime source from webOS bridge"
      : "TV reachable but pairing is incomplete"
    : deviceState?.stale
      ? "Showing cached state from the last successful TV refresh"
    : page.featuredDevice?.nowPlaying?.subtitle;
  const liveInputItems = deviceState?.inputs?.length
    ? deviceState.inputs
        .filter((item) => item.appId)
        .map((item) => ({ label: item.label, appId: item.appId }))
    : (page.featuredDevice?.apps ?? []).map((item) => ({ label: item, appId: "" }));
  const preferredLaunchAppIds = [
    "youtube.leanback.v4",
    "com.fpt.fptplay",
    "spotify-beehive",
    "com.webos.app.browser",
    "vieplay.vn",
    "com.vtvgotv.app",
  ];
  const preferredLaunchAppIconMap = {
    "youtube.leanback.v4": youtubeIcon,
    "com.fpt.fptplay": fptPlayIcon,
    "spotify-beehive": spotifyIcon,
    "com.webos.app.browser": browserIcon,
    "vieplay.vn": vieonIcon,
    "com.vtvgotv.app": vtvGoIcon,
  };
  const preferredLaunchAppFallbacks = [
    { id: "youtube.leanback.v4", title: "YouTube", icon: youtubeIcon },
    { id: "com.fpt.fptplay", title: "FPT Play", icon: fptPlayIcon },
    { id: "spotify-beehive", title: "Spotify", icon: spotifyIcon },
    { id: "com.webos.app.browser", title: "Browser", icon: browserIcon },
    { id: "vieplay.vn", title: "VieON", icon: vieonIcon },
    { id: "com.vtvgotv.app", title: "VTVGo", icon: vtvGoIcon },
  ];
  const liveLaunchPoints = Array.isArray(deviceState?.launchPoints) ? deviceState.launchPoints : [];
  const preferredLaunchApps = preferredLaunchAppIds
    .map((appId) => liveLaunchPoints.find((item) => item?.id === appId))
    .filter(Boolean);
  const featuredLaunchApps = preferredLaunchApps.length
    ? preferredLaunchApps
    : liveLaunchPoints.length
      ? liveLaunchPoints.filter((item) => item?.id && item?.icon).slice(0, 6)
      : preferredLaunchAppFallbacks;
  const hasLiveForegroundApp = Boolean(deviceState?.reachable && !deviceState?.stale && deviceState?.foregroundAppId);
  const currentForegroundAppTitle = hasLiveForegroundApp
    ? (deviceState?.foregroundAppTitle ?? deviceState?.foregroundAppId ?? "Unknown")
    : "Offline";
  const currentForegroundAppStartedAt = hasLiveForegroundApp ? (deviceState?.foregroundAppStartedAt ?? null) : null;
  const currentForegroundAppDurationSeconds = hasLiveForegroundApp ? (deviceState?.foregroundAppDurationSeconds ?? null) : null;
  const filteredDeviceAppHistory = deviceAppHistory.filter((item) => {
    const payload = item.payloadJson ?? {};
    const appName = String(payload.title ?? payload.appId ?? "").toLowerCase();
    const nameFilter = deviceAppHistoryNameFilter.trim().toLowerCase();
    if (nameFilter && !appName.includes(nameFilter)) {
      return false;
    }
    if (deviceAppHistoryDateFilter) {
      const startedDate = formatDateForFilter(payload.startedAt);
      if (startedDate !== deviceAppHistoryDateFilter) {
        return false;
      }
    }
    return true;
  });
  const liveBridgeConnectivity = bridgeState
    ? bridgeState.connected
      ? "Serial link active"
      : bridgeState.error
        ? "Bridge error"
        : "Idle"
    : "Waiting for bridge";
  const liveRoomNodeConnectivity = roomNodeState
    ? roomNodeState.connected
      ? "MQTT online"
      : roomNodeState.lastError
        ? "MQTT error"
        : "Offline"
    : "Waiting for node";
  const liveRoomNodeSecondaryConnectivity = roomNodeSecondaryState
    ? roomNodeSecondaryState.connected
      ? "MQTT online"
      : roomNodeSecondaryState.lastError
        ? "MQTT error"
        : "Offline"
    : "Waiting for node";
  const aTravelSteps = (motionState.a?.travel && motionState.a.travel > 0)
    ? motionState.a.travel
    : (page.bridge?.bAxisTuning?.defaultATravelSteps ?? 0);
  const bTravelSteps = (motionState.b?.travel && motionState.b.travel > 0)
    ? motionState.b.travel
    : (page.bridge?.bAxisTuning?.defaultTravelSteps ?? 0);
  const aMmPerStep = page.bridge?.bAxisTuning?.aMmPerStep ?? 1;
  const bMmPerStep = page.bridge?.bAxisTuning?.bMmPerStep ?? 1;
  const aTravelMm = aTravelSteps * aMmPerStep;
  const bTravelMm = bTravelSteps * bMmPerStep;
  const byj1State = byjStates.byj1 ?? null;
  const byj2State = byjStates.byj2 ?? null;
  const byj1CurrentMm = Number.isFinite(byj1State?.mm) ? byj1State.mm : ((byj1State?.pos ?? 0) * byj1MmPerStep);
  const byj1TargetMm = (byj1State?.target ?? 0) * byj1MmPerStep;
  const isAquariumPage = Boolean(page.bridge && page.pumpControl);
  const aquariumSectionTabs = isAquariumPage
    ? [
        { id: "control", label: "Control", icon: "control" },
        { id: "servo", label: "Servo", icon: "servo" },
        { id: "workspace", label: "AB Workspace", icon: "control" },
      ]
    : (page.deviceStrip ?? []);
  const hasAquariumTabs = Boolean(page.bridge?.bAxisTuning && aquariumSectionTabs.length > 1);
  const showControlTab = !hasAquariumTabs || activeModuleTab === "control";
  const showServoTab = hasAquariumTabs && activeModuleTab === "servo";
  const showWorkspaceTab = hasAquariumTabs && activeModuleTab === "workspace";
  const showFeaturedDeviceTab = !page.featuredDevice || activeRoomDeviceTab === "featured";
  const showSwitchPanelTab = !page.featuredDevice || activeRoomDeviceTab === "switch-panel";
  const showRoomNodeTab = !page.featuredDevice || activeRoomDeviceTab === "room-node";
  const showRoomNodeSecondaryTab = !page.featuredDevice || activeRoomDeviceTab === "room-node-secondary";
  const currentAMm = (motionState.ab?.posA ?? motionState.a?.pos ?? 0) * aMmPerStep;
  const currentBMm = (motionState.ab?.posB ?? motionState.b?.pos ?? 0) * bMmPerStep;
  const liveTargetAMm = (motionState.ab?.targetA ?? motionState.a?.target ?? 0) * aMmPerStep;
  const liveTargetBMm = (motionState.ab?.targetB ?? motionState.b?.target ?? 0) * bMmPerStep;
  const anyAxisMoving = Boolean(motionState.a?.moving || motionState.b?.moving);
  const abCoordinatorActive = Boolean(motionState.ab?.active);
  const anyAbMotion = Boolean(anyAxisMoving || abCoordinatorActive);
  const anyAxisHoming = Boolean(
    (motionState.a?.homing && motionState.a.homing !== "idle")
    || (motionState.b?.homing && motionState.b.homing !== "idle"),
  );
  const anyAxisEnabled = Boolean(motionState.a?.enabled || motionState.b?.enabled);
  const bothAxesKnown = Boolean(motionState.a && motionState.b);
  const bothAxesDisabled = Boolean(
    bothAxesKnown
    && motionState.a?.enabled === false
    && motionState.b?.enabled === false,
  );
  const bothAxesHomed = Boolean(motionState.a?.homed && motionState.b?.homed);
  const abCommandLocked = Boolean(anyAbMotion || anyAxisHoming || bAxisCommandPending);
  const abMotionLabel = anyAxisHoming
    ? "Homing"
    : anyAbMotion
      ? "Moving"
      : (motionState.ab?.parked && anyAxisEnabled && bothAxesHomed)
        ? "Parked"
      : bothAxesDisabled
        ? "Stopped"
      : (anyAxisEnabled && bothAxesHomed)
        ? "Idle"
        : "Idle";
  const effectiveTargetA = selectedABTarget?.aTargetMm ?? liveTargetAMm;
  const effectiveTargetB = selectedABTarget?.bTargetMm ?? liveTargetBMm;
  const currentAMarker = aTravelMm > 0 ? Math.max(0, Math.min(100, (currentAMm / aTravelMm) * 100)) : 0;
  const currentBMarker = bTravelMm > 0 ? Math.max(0, Math.min(100, (currentBMm / bTravelMm) * 100)) : 0;
  const targetAMarker = aTravelMm > 0 ? Math.max(0, Math.min(100, (effectiveTargetA / aTravelMm) * 100)) : 0;
  const targetBMarker = bTravelMm > 0 ? Math.max(0, Math.min(100, (effectiveTargetB / bTravelMm) * 100)) : 0;
  const selectedAMarker = aTravelMm > 0 && selectedABTarget ? Math.max(0, Math.min(100, (selectedABTarget.aTargetMm / aTravelMm) * 100)) : null;
  const selectedBMarker = bTravelMm > 0 && selectedABTarget ? Math.max(0, Math.min(100, (selectedABTarget.bTargetMm / bTravelMm) * 100)) : null;
  const axisAStatusKey = motionState.a?.homing && motionState.a.homing !== "idle"
    ? "homing"
    : motionState.a?.homed
      ? "homed"
      : "not-homed";
  const axisBStatusKey = motionState.b?.homing && motionState.b.homing !== "idle"
    ? "homing"
    : motionState.b?.homed
      ? "homed"
      : "not-homed";
  const axisAStatusLabel = axisAStatusKey === "homing"
    ? "Axis A: Homing"
    : axisAStatusKey === "homed"
      ? "Axis A: Homed"
      : "Axis A: Not Homed";
  const axisBStatusLabel = axisBStatusKey === "homing"
    ? "Axis B: Homing"
    : axisBStatusKey === "homed"
      ? "Axis B: Homed"
      : "Axis B: Not Homed";
  const parsedSelectedATargetMm = Number.parseFloat(selectedATargetMmInput);
  const parsedSelectedBTargetMm = Number.parseFloat(selectedBTargetMmInput);
  const selectedTargetReached = Boolean(
    selectedABTarget
    && !anyAbMotion
    && Math.abs(currentAMm - selectedABTarget.aTargetMm) <= Math.max(0.51, aMmPerStep * 2)
    && Math.abs(currentBMm - selectedABTarget.bTargetMm) <= Math.max(0.51, bMmPerStep * 2),
  );
  const selectedTargetState = selectedABTarget
    ? anyAbMotion
      ? "moving"
      : selectedTargetReached
        ? "reached"
        : "idle"
    : "idle";
  const roomSwitchItems = [
    ...(page.switchPanel && page.roomNode
      ? (page.roomNode.relays ?? []).map((relay) => ({
          id: `primary-${relay.key}`,
          label: `${page.roomNode.title} ${relay.label}`,
          nodeKind: "primary",
          relayKey: relay.key,
          commandType: "relay",
          relayOn: Boolean(roomNodeState?.relays?.[relay.key]),
          ledModes: page.roomNode.ledModes ?? [],
          ledMode:
            page.roomNode.ledModeScopedByRelay === false
              ? (roomNodeState?.ledMode ?? "auto")
              : (roomNodeState?.ledModes?.[relay.key] ?? "auto"),
        }))
      : []),
    ...(page.switchPanel && page.roomNodeSecondary
      ? (page.roomNodeSecondary.relays ?? []).map((relay) => ({
          id: `secondary-${relay.key}`,
          label: `${page.roomNodeSecondary.title} ${relay.label}`,
          nodeKind: "secondary",
          relayKey: relay.key,
          commandType: "relay",
          relayOn: Boolean(roomNodeSecondaryState?.relays?.[relay.key]),
          ledModes: page.roomNodeSecondary.ledModes ?? [],
          ledMode:
            page.roomNodeSecondary.ledModeScopedByRelay === false
              ? (roomNodeSecondaryState?.ledMode ?? "auto")
              : (roomNodeSecondaryState?.ledModes?.[relay.key] ?? "auto"),
        }))
      : []),
    ...(page.switchPanel && page.roomNodeSecondary?.remoteRelayLabel
      ? [
          {
            id: "secondary-remote-relay",
            label: `${page.roomNodeSecondary.title} ${page.roomNodeSecondary.remoteRelayLabel}`,
            nodeKind: "secondary",
            relayKey: "remoteRelay",
            commandType: "remote",
            relayOn: Boolean(roomNodeSecondaryState?.remoteRelay),
            ledModes: [],
            ledMode: "auto",
          },
        ]
      : []),
  ].map((item) => ({
    ...item,
    ledIsAuto: item.ledMode === "auto",
    ledIsOn: item.ledMode !== "auto" && item.ledMode !== "off",
    hasLedControl: item.ledModes.length > 0,
  }));
  function renderRemoteUtilityIcon(action) {
    if (action === "Power") {
      return (
        <svg viewBox="0 0 24 24" aria-hidden="true">
          <path d="M12 3v8" />
          <path d="M7.05 5.8a8 8 0 1 0 9.9 0" />
        </svg>
      );
    }
    if (action === "Mute") {
      return (
        <svg viewBox="0 0 24 24" aria-hidden="true">
          <path d="M4 10h4l5-4v12l-5-4H4z" />
          <path d="M19 9l-6 6" />
        </svg>
      );
    }
    if (action === "Source") {
      return (
        <svg viewBox="0 0 24 24" aria-hidden="true">
          <rect x="4" y="6" width="12" height="10" rx="2" />
          <path d="M18 12h2" />
          <path d="M17 9l3 3-3 3" />
        </svg>
      );
    }
    if (action === "Home") {
      return (
        <svg viewBox="0 0 24 24" aria-hidden="true">
          <path d="M4 11.5 12 5l8 6.5" />
          <path d="M6.5 10.5V19h11v-8.5" />
          <path d="M10 19v-4.5h4V19" />
        </svg>
      );
    }
    if (action === "Back") {
      return (
        <svg viewBox="0 0 24 24" aria-hidden="true">
          <path d="M10 7 5 12l5 5" />
          <path d="M6 12h7a6 6 0 1 1 0 12" />
        </svg>
      );
    }
    return (
      <svg viewBox="0 0 24 24" aria-hidden="true">
        <path d="M6 7h12" />
        <path d="M6 12h12" />
        <path d="M6 17h12" />
        <circle cx="9" cy="7" r="1.5" />
        <circle cx="15" cy="12" r="1.5" />
        <circle cx="11" cy="17" r="1.5" />
      </svg>
    );
  }

  async function syncFeaturedDevice(delayMs = 1200) {
    if (!page.featuredDevice?.apiPath) {
      return;
    }

    if (syncTimerRef.current) {
      window.clearTimeout(syncTimerRef.current);
    }

    syncTimerRef.current = window.setTimeout(async () => {
      const baseUrl = getFeaturedBaseUrl();
      try {
        const response = await fetch(`${baseUrl}/status`);
        if (!response.ok) {
          throw new Error(`status ${response.status}`);
        }
        const payload = await response.json();
        setDeviceState(reconcileIncomingDeviceState(payload));
        setDeviceError("");
      } catch (error) {
        setDeviceError(error.message);
      }
    }, delayMs);
  }

  function showVolumeHudTemporarily(durationMs = 2400) {
    setShowVolumeHud(true);
    if (volumeHudTimerRef.current) {
      window.clearTimeout(volumeHudTimerRef.current);
    }
    volumeHudTimerRef.current = window.setTimeout(() => {
      setShowVolumeHud(false);
      volumeHudTimerRef.current = null;
    }, durationMs);
  }

  async function setFeaturedVolume(targetVolume) {
    if (!page.featuredDevice?.apiPath) {
      return;
    }

    const nextVolume = clampVolume(targetVolume);
    pendingVolumeSyncRef.current = true;
    pendingVolumeTargetRef.current = nextVolume;
    setVolumeHudValue(nextVolume);
    showVolumeHudTemporarily();
    const baseUrl = getFeaturedBaseUrl();

    try {
      const response = await fetch(`${baseUrl}/command`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ command: "set_volume", volume: nextVolume }),
      });
      const payload = await response.json();
      if (!response.ok) {
        throw new Error(payload.error || `status ${response.status}`);
      }
      if (payload.state) {
        const reconciledState = reconcileIncomingDeviceState(payload.state);
        setDeviceState(reconciledState);
        if (reconciledState?.volume != null) {
          setVolumeHudValue(clampVolume(reconciledState.volume));
        }
      }
      setDeviceError("");
      syncFeaturedDevice(180);
    } catch (error) {
      setDeviceError(error.message);
      pendingVolumeSyncRef.current = false;
      pendingVolumeTargetRef.current = null;
      syncFeaturedDevice(250);
    }
  }

  function resolveVolumeFromPointer(clientY) {
    const track = volumeHudTrackRef.current;
    if (!track) {
      return clampVolume(liveVolume ?? 0);
    }

    const bounds = track.getBoundingClientRect();
    const offsetY = Math.max(0, Math.min(bounds.height, clientY - bounds.top));
    const ratio = 1 - offsetY / bounds.height;
    return clampVolume(ratio * 100);
  }

  function handleVolumePointerDown(event) {
    const nextVolume = resolveVolumeFromPointer(event.clientY);
    volumeHudPointerIdRef.current = event.pointerId;
    volumeHudDraggingRef.current = true;
    setVolumeHudDragging(true);
    setVolumeHudValue(nextVolume);
    setShowVolumeHud(true);
    if (volumeHudTimerRef.current) {
      window.clearTimeout(volumeHudTimerRef.current);
      volumeHudTimerRef.current = null;
    }
    event.currentTarget.setPointerCapture(event.pointerId);
  }

  function handleVolumePointerMove(event) {
    if (volumeHudPointerIdRef.current !== event.pointerId) {
      return;
    }
    setVolumeHudValue(resolveVolumeFromPointer(event.clientY));
  }

  function finishVolumePointerInteraction(event) {
    if (volumeHudPointerIdRef.current !== event.pointerId) {
      return;
    }

    if (event.currentTarget.hasPointerCapture(event.pointerId)) {
      event.currentTarget.releasePointerCapture(event.pointerId);
    }
    volumeHudPointerIdRef.current = null;
    volumeHudDraggingRef.current = false;
    setVolumeHudDragging(false);
    setFeaturedVolume(resolveVolumeFromPointer(event.clientY));
  }

  async function sendFeaturedCommand(commandPayload) {
    if (!page.featuredDevice?.apiPath) {
      return;
    }
    const baseUrl = getFeaturedBaseUrl();
    try {
      applyOptimisticCommand(commandPayload);
      if (commandPayload.command === "volume_up" || commandPayload.command === "volume_down") {
        setVolumeHudValue(clampVolume((deviceState?.volume ?? volumeHudValueRef.current ?? 0) + (commandPayload.command === "volume_up" ? 1 : -1)));
        showVolumeHudTemporarily();
      }
      if (commandPayload.command === "turn_on" || commandPayload.command === "turn_off") {
        setPendingPowerCommand(commandPayload.command);
      }
      const response = await fetch(`${baseUrl}/command`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(commandPayload),
      });
      const payload = await response.json();
      if (!response.ok) {
        throw new Error(payload.error || `status ${response.status}`);
      }
      setDeviceError("");
      syncFeaturedDevice(commandPayload.command === "turn_on" ? 3500 : 900);
    } catch (error) {
      setDeviceError(error.message);
      if (commandPayload.command === "turn_on" || commandPayload.command === "turn_off") {
        setPendingPowerCommand("");
      }
      syncFeaturedDevice(500);
    }
  }

  async function refreshFeaturedDevice() {
    if (!page.featuredDevice?.apiPath) {
      return;
    }
    const baseUrl = getFeaturedBaseUrl();
    try {
      const response = await fetch(`${baseUrl}/refresh`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: "{}",
      });
      const payload = await response.json();
      if (!response.ok) {
        throw new Error(payload.error || `status ${response.status}`);
      }
      setDeviceState(reconcileIncomingDeviceState(payload));
      setDeviceError("");
    } catch (error) {
      setDeviceError(error.message);
      syncFeaturedDevice(300);
    }
  }

  async function refreshFeaturedApps() {
    if (!page.featuredDevice?.apiPath) {
      return;
    }
    const baseUrl = getFeaturedBaseUrl();
    try {
      const response = await fetch(`${baseUrl}/apps`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: "{}",
      });
      const payload = await response.json();
      if (!response.ok) {
        throw new Error(payload.error || `status ${response.status}`);
      }
      if (payload.state) {
        setDeviceState(reconcileIncomingDeviceState(payload.state));
      } else {
        setDeviceState((current) => ({ ...(current ?? {}), ...payload }));
      }
      setDeviceAppsError("");
    } catch (error) {
      setDeviceAppsError(error.message);
    }
  }

  async function refreshFeaturedAppHistory() {
    if (!page.featuredDevice?.apiPath) {
      return;
    }
    const baseUrl = getFeaturedBaseUrl();
    try {
      const response = await fetch(`${baseUrl}/app-history?limit=100`);
      const payload = await response.json();
      if (!response.ok) {
        throw new Error(payload.error || `status ${response.status}`);
      }
      setDeviceAppHistory(payload.items ?? []);
      setDeviceAppHistoryError("");
    } catch (error) {
      setDeviceAppHistoryError(error.message);
    }
  }

  async function refreshBridgeStatus() {
    if (!page.bridge?.apiPath) {
      return;
    }
    const baseUrl = getBridgeBaseUrl();
    try {
      const [statusResponse, logsResponse] = await Promise.all([
        fetch(`${baseUrl}/status`),
        fetch(`${baseUrl}/logs?limit=40`),
      ]);
      const statusPayload = await statusResponse.json();
      const logsPayload = await logsResponse.json();
      if (!statusResponse.ok) {
        throw new Error(statusPayload.error || `status ${statusResponse.status}`);
      }
      if (!logsResponse.ok) {
        throw new Error(logsPayload.error || `logs ${logsResponse.status}`);
      }
      const logItems = logsPayload.items ?? [];
      setBridgeState(statusPayload);
      setBridgeLogs(logItems.slice(-6));
      applyBridgeLinesToControls(logItems.map((item) => item.payload));
      applyMotionLinesToState([statusPayload.lastLine].filter(Boolean));
      setBridgeError("");
    } catch (error) {
      setBridgeError(error.message);
    }
  }

  async function sendBridgeTextCommand(text) {
    if (!page.bridge?.apiPath) {
      return;
    }
    const commandText = String(text ?? "").trim();
    if (!commandText) {
      return;
    }
    const baseUrl = getBridgeBaseUrl();
    try {
      setBAxisCommandPending(true);
      const response = await fetch(`${baseUrl}/send`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ text: commandText }),
      });
      const payload = await response.json().catch(() => ({}));
      if (!response.ok) {
        throw new Error(payload.error || `send ${response.status}`);
      }
      setBridgeError("");
      window.setTimeout(() => {
        refreshBridgeStatus();
      }, 250);
    } catch (error) {
      setBridgeError(error.message);
    } finally {
      setBAxisCommandPending(false);
    }
  }

  async function requestBridgeStatusSnapshot(delayMs = 180) {
    if (!page.bridge?.apiPath) {
      return;
    }
    const baseUrl = getBridgeBaseUrl();
    window.setTimeout(async () => {
      try {
        await fetch(`${baseUrl}/send`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ text: "status" }),
        });
        window.setTimeout(() => {
          refreshBridgeStatus();
        }, 120);
      } catch (error) {
        setBridgeError(error.message);
      }
    }, delayMs);
  }

  async function applyBAxisTravel() {
    const value = Number.parseInt(bAxisTravelSteps, 10);
    if (!Number.isFinite(value) || value < 100) {
      setBridgeError("travel must be >= 100");
      return;
    }
    await sendBridgeTextCommand(`axis b travel ${value}`);
  }

  async function applyBAxisDecelWindow() {
    const value = Number.parseInt(bAxisDecelWindowSteps, 10);
    if (!Number.isFinite(value) || value < 10) {
      setBridgeError("decel window must be >= 10");
      return;
    }
    await sendBridgeTextCommand(`axis b decel_window ${value}`);
  }

  async function applyBAxisGoto() {
    const value = Number.parseInt(bAxisGotoPosition, 10);
    if (!Number.isFinite(value)) {
      setBridgeError("goto position is invalid");
      return;
    }
    await sendBridgeTextCommand(`axis b goto ${value}`);
  }

  async function runBridgeCommandSequence(commands, delayMs = 180) {
    for (const command of commands) {
      await sendBridgeTextCommand(command);
      if (delayMs > 0) {
        await new Promise((resolve) => window.setTimeout(resolve, delayMs));
      }
    }
  }

  async function scanBAxisTravel() {
    if (abCommandLocked) {
      setBridgeError("stop AB motion before sending another axis command");
      return;
    }
    await sendBridgeTextCommand("axis b scan");
    await requestBridgeStatusSnapshot(300);
  }

  async function homeAAxis() {
    if (abCommandLocked) {
      setBridgeError("stop AB motion before sending another axis command");
      return;
    }
    await sendBridgeTextCommand("axis a home");
    await requestBridgeStatusSnapshot(300);
  }

  async function homeBAxis() {
    if (abCommandLocked) {
      setBridgeError("stop AB motion before sending another axis command");
      return;
    }
    await sendBridgeTextCommand("axis b home");
    await requestBridgeStatusSnapshot(300);
  }

  async function homeABAxes() {
    if (abCommandLocked) {
      setBridgeError("stop AB motion before sending another AB command");
      return;
    }
    setMotionState((current) => ({
      ...current,
      ab: current.ab
        ? {
            ...current.ab,
            parked: false,
          }
        : current.ab,
    }));
    await sendBridgeTextCommand("ab home");
  }

  async function scanAAxisTravel() {
    if (abCommandLocked) {
      setBridgeError("stop AB motion before sending another axis command");
      return;
    }
    await sendBridgeTextCommand("axis a scan");
    await requestBridgeStatusSnapshot(300);
  }

  async function sendABGoto(aTarget, bTarget) {
    if (abCommandLocked) {
      setBridgeError("stop AB motion before sending another AB command");
      return;
    }
    setMotionState((current) => ({
      ...current,
      ab: current.ab
        ? {
            ...current.ab,
            parked: false,
          }
        : current.ab,
    }));
    await sendBridgeTextCommand(`ab goto ${aTarget} ${bTarget}`);
  }

  async function parkABAxes() {
    if (abCommandLocked) {
      setBridgeError("stop AB motion before sending another AB command");
      return;
    }
    setSelectedABTarget({ aTargetMm: 0, bTargetMm: 0 });
    setSelectedATargetMmInput("0.00");
    setSelectedBTargetMmInput("0.00");
    setMotionState((current) => ({
      ...current,
      ab: {
        ...(current.ab ?? {}),
        parked: true,
      },
    }));
    await sendBridgeTextCommand("ab park");
  }

  function validateABTargetMm(aTargetMm, bTargetMm) {
    if (!Number.isFinite(aTargetMm) || !Number.isFinite(bTargetMm)) {
      setBridgeError("enter valid A/B targets in mm");
      return null;
    }
    if (!motionState.a?.homed || !motionState.b?.homed) {
      setBridgeError("home AB before sending an AB target");
      return null;
    }
    if (aTargetMm < 0 || bTargetMm < 0 || aTargetMm > aTravelMm || bTargetMm > bTravelMm) {
      setBridgeError("target mm is outside scanned travel");
      return null;
    }

    return {
      aTargetMm,
      bTargetMm,
      aTargetSteps: Math.round(aTargetMm / aMmPerStep),
      bTargetSteps: Math.round(bTargetMm / bMmPerStep),
    };
  }

  function parseABTargetFromInputs() {
    const aTargetMm = Number.parseFloat(selectedATargetMmInput);
    const bTargetMm = Number.parseFloat(selectedBTargetMmInput);
    return validateABTargetMm(aTargetMm, bTargetMm);
  }

  async function applyABTargetMm(targetLike) {
    const target = validateABTargetMm(targetLike?.aTargetMm, targetLike?.bTargetMm);
    if (!target) {
      return;
    }
    setSelectedABTarget({ aTargetMm: target.aTargetMm, bTargetMm: target.bTargetMm });
    setSelectedATargetMmInput(target.aTargetMm.toFixed(2));
    setSelectedBTargetMmInput(target.bTargetMm.toFixed(2));
    await sendABGoto(target.aTargetSteps, target.bTargetSteps);
  }

  async function applySelectedABTarget() {
    const target = parseABTargetFromInputs();
    if (!target) {
      return;
    }
    setSelectedABTarget({ aTargetMm: target.aTargetMm, bTargetMm: target.bTargetMm });
    setSelectedATargetMmInput(target.aTargetMm.toFixed(2));
    setSelectedBTargetMmInput(target.bTargetMm.toFixed(2));
    await sendABGoto(target.aTargetSteps, target.bTargetSteps);
  }

  async function sendABNowFromInputs() {
    const target = parseABTargetFromInputs();
    if (!target) {
      return;
    }
    setSelectedABTarget({ aTargetMm: target.aTargetMm, bTargetMm: target.bTargetMm });
    setSelectedATargetMmInput(target.aTargetMm.toFixed(2));
    setSelectedBTargetMmInput(target.bTargetMm.toFixed(2));
    await sendABGoto(target.aTargetSteps, target.bTargetSteps);
  }

  async function stopABMotion() {
    setMotionState((current) => ({
      a: current.a
        ? {
            ...current.a,
            enabled: false,
            moving: false,
            homing: "idle",
            homed: false,
            vel: 0,
          }
        : current.a,
      b: current.b
        ? {
            ...current.b,
            enabled: false,
            moving: false,
            homing: "idle",
            homed: false,
            vel: 0,
          }
        : current.b,
      ab: {
        active: false,
        posA: current.a?.pos ?? 0,
        targetA: current.a?.target ?? current.a?.pos ?? 0,
        posB: current.b?.pos ?? 0,
        targetB: current.b?.target ?? current.b?.pos ?? 0,
        stepsDone: 0,
        stepsTotal: 0,
        intervalUs: 0,
        cruiseUs: 0,
        startUs: 0,
        parked: false,
      },
    }));
    await runBridgeCommandSequence(["ab stop", "axis a stop", "axis b stop"], 120);
    await requestBridgeStatusSnapshot(300);
  }

  async function refreshServoStatus() {
    await sendBridgeTextCommand("servo status");
  }

  async function applyServoAngleCommand(servoKey, angleValue) {
    const angle = Number.parseInt(String(angleValue ?? ""), 10);
    if (!servoKey) {
      setBridgeError("select a servo first");
      return;
    }
    if (!Number.isFinite(angle) || angle < 0 || angle > 180) {
      setBridgeError("servo angle must be 0..180");
      return;
    }
    await sendBridgeTextCommand(`servo ${servoKey} angle ${angle}`);
    setServoStates((current) => ({
      ...current,
      [servoKey]: {
        ...(current[servoKey] ?? {}),
        angle,
        pulseUs: Math.round(500 + (angle * 2000) / 180),
      },
    }));
  }

  async function refreshByjStatus() {
    await sendBridgeTextCommand("byj status");
  }

  async function homeByj1() {
    await sendBridgeTextCommand("byj byj1 enable on");
    await new Promise((resolve) => window.setTimeout(resolve, 120));
    await sendBridgeTextCommand("byj byj1 home");
  }

  async function stopByj1() {
    await sendBridgeTextCommand("byj byj1 stop");
  }

  async function parkByj1() {
    await runBridgeCommandSequence(["byj byj1 enable on", "byj byj1 goto 0"], 120);
  }

  async function applyByj1GotoMm() {
    const targetMm = Number.parseFloat(byj1TargetMmInput);
    if (!Number.isFinite(targetMm) || targetMm < 0) {
      setBridgeError("BYJ1 target mm must be >= 0");
      return;
    }

    const targetSteps = Math.round(targetMm / byj1MmPerStep);
    await sendBridgeTextCommand(`byj byj1 goto ${targetSteps}`);
  }

  async function runByj2Move() {
    const steps = Number.parseInt(byj2StepInput, 10);
    if (!Number.isFinite(steps) || steps <= 0) {
      setBridgeError("BYJ2 step must be > 0");
      return;
    }

    const signedSteps = byj2Direction === "-" ? -steps : steps;
    await runBridgeCommandSequence(["byj byj2 enable on", `byj byj2 move ${signedSteps}`], 120);
  }

  async function pauseByj2() {
    await sendBridgeTextCommand("byj byj2 stop");
  }

  async function jogByj2(direction) {
    await runBridgeCommandSequence(["byj byj2 enable on", `byj byj2 jog ${direction}`], 120);
  }

  function updateServoAngleInput(servoKey, nextValue) {
    setServoAngleInputs((current) => ({
      ...current,
      [servoKey]: nextValue,
    }));
  }

  function selectABTarget(target) {
    const aTargetMm = target?.aTargetMm;
    const bTargetMm = target?.bTargetMm;
    if (!Number.isFinite(aTargetMm) || !Number.isFinite(bTargetMm)) {
      return;
    }
    setSelectedABTarget({ aTargetMm, bTargetMm });
    setSelectedATargetMmInput(String(aTargetMm));
    setSelectedBTargetMmInput(String(bTargetMm));
  }

  function focusFeaturedDevice() {
    if (!featuredDeviceCardRef.current) {
      return;
    }
    featuredDeviceCardRef.current.scrollIntoView({ behavior: "smooth", block: "start" });
  }

  function focusRoomNode() {
    if (!roomNodeCardRef.current) {
      return;
    }
    roomNodeCardRef.current.scrollIntoView({ behavior: "smooth", block: "start" });
  }

  function focusRoomNodeSecondary() {
    if (!roomNodeSecondaryCardRef.current) {
      return;
    }
    roomNodeSecondaryCardRef.current.scrollIntoView({ behavior: "smooth", block: "start" });
  }

  function focusPageSection(sectionId) {
    const element = sectionRefs.current[sectionId];
    if (!element) {
      return;
    }
    element.scrollIntoView({ behavior: "smooth", block: "start" });
  }

  async function sendRoomNodeCommand(commandPayload) {
    const baseUrl = getRoomNodeBaseUrl();
    if (!baseUrl) {
      return;
    }
    try {
      const response = await fetch(`${baseUrl}/command`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(commandPayload),
      });
      const payload = await response.json().catch(() => ({}));
      if (!response.ok) {
        throw new Error(payload.error || `command ${response.status}`);
      }
      if (payload.state) {
        setRoomNodeState(payload.state);
      }
      setRoomNodeError("");
    } catch (error) {
      setRoomNodeError(error.message);
    }
  }

  async function sendRoomNodeSecondaryCommand(commandPayload) {
    const baseUrl = getRoomNodeSecondaryBaseUrl();
    if (!baseUrl) {
      return;
    }
    try {
      const response = await fetch(`${baseUrl}/command`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(commandPayload),
      });
      const payload = await response.json().catch(() => ({}));
      if (!response.ok) {
        throw new Error(payload.error || `command ${response.status}`);
      }
      if (payload.state) {
        setRoomNodeSecondaryState(payload.state);
      }
      setRoomNodeSecondaryError("");
    } catch (error) {
      setRoomNodeSecondaryError(error.message);
    }
  }

  async function refreshUploads() {
    const baseUrl = getUploadBaseUrl();
    if (!baseUrl) {
      return;
    }
    try {
      const response = await fetch(baseUrl);
      const payload = await response.json();
      if (!response.ok) {
        throw new Error(payload.error || `uploads ${response.status}`);
      }
      setUploadItems(payload.items ?? []);
      setUploadError("");
    } catch (error) {
      setUploadError(error.message);
    }
  }

  async function uploadDashboardFile() {
    const baseUrl = getUploadBaseUrl();
    const file = uploadInputRef.current?.files?.[0];
    if (!baseUrl) {
      return;
    }
    if (!file) {
      setUploadError("choose a file first");
      return;
    }

    const formData = new FormData();
    formData.append("file", file);

    try {
      setUploadingFile(true);
      const response = await fetch(baseUrl, {
        method: "POST",
        body: formData,
      });
      const payload = await response.json();
      if (!response.ok) {
        throw new Error(payload.error || `upload ${response.status}`);
      }
      setUploadItems(payload.items ?? []);
      setUploadError("");
      setUploadInfo(payload.item?.path ? `Saved to ${payload.item.path}` : "Upload complete");
      if (uploadInputRef.current) {
        uploadInputRef.current.value = "";
      }
    } catch (error) {
      setUploadError(error.message);
    } finally {
      setUploadingFile(false);
    }
  }

  function renderUploadPanel() {
    if (!page.uploadPanel) {
      return null;
    }

    return (
      <section
        ref={(element) => {
          sectionRefs.current.upload = element;
        }}
        className="featured-device-card dashboard-upload-card"
      >
        <div className="featured-device-copy">
          <h3>{page.uploadPanel.title}</h3>
          {page.uploadPanel.note ? <p>{page.uploadPanel.note}</p> : null}
        </div>

        <div className="dashboard-upload-panel">
          <div className="dashboard-upload-row">
            <label className="dashboard-upload-picker" htmlFor="dashboard-upload-input">
              <span>Choose File</span>
              <input
                id="dashboard-upload-input"
                ref={uploadInputRef}
                type="file"
              />
            </label>
            <button
              className="ghost-pill"
              type="button"
              onClick={uploadDashboardFile}
              disabled={uploadingFile}
            >
              {uploadingFile ? "Uploading..." : "Upload"}
            </button>
            <button
              className="ghost-pill"
              type="button"
              onClick={refreshUploads}
              disabled={uploadingFile}
            >
              Refresh
            </button>
          </div>
          {uploadInfo ? <p className="dashboard-upload-info">{uploadInfo}</p> : null}
          {uploadError ? <p className="dashboard-upload-error">{uploadError}</p> : null}

          <div className="dashboard-upload-list">
            {uploadItems.length ? uploadItems.map((item) => (
              <article key={`${item.path}-${item.modifiedAt}`} className="dashboard-upload-item">
                <strong>{item.name}</strong>
                <span>{formatUploadSize(item.size)}</span>
                <span>{formatUploadModifiedAt(item.modifiedAt)}</span>
                <code>{item.path}</code>
              </article>
            )) : (
              <p className="dashboard-upload-empty">No uploaded files yet.</p>
            )}
          </div>
        </div>
      </section>
    );
  }

  async function setSwitchRelay(nodeKind, relayKey, nextValue, commandType = "relay") {
    if (commandType === "remote") {
      await sendRoomNodeSecondaryCommand({
        action: "toggle_remote",
      });
      return;
    }

    if (nodeKind === "primary") {
      await sendRoomNodeCommand({
        action: "set_relay",
        channel: relayKey,
        value: nextValue,
      });
      return;
    }

    await sendRoomNodeSecondaryCommand({
      action: "set_relay",
      channel: relayKey,
      value: nextValue,
    });
  }

  async function setSwitchLedMode(nodeKind, relayKey, mode) {
    if (nodeKind === "primary") {
      await sendRoomNodeCommand({
        action: "set_led_mode",
        ...(page.roomNode?.ledModeScopedByRelay === false ? {} : { channel: relayKey }),
        mode,
      });
      return;
    }

    await sendRoomNodeSecondaryCommand({
      action: "set_led_mode",
      ...(page.roomNodeSecondary?.ledModeScopedByRelay === false ? {} : { channel: relayKey }),
      mode,
    });
  }

  function renderServoModeToggle(modeKey) {
    const isManual = servoControlModes[modeKey] === "manual";
    return (
      <div className="servo-mode-toggle servo-mode-toggle-rocker">
        <label className={`ios-toggle ios-toggle-small servo-mode-rocker ${isManual ? "is-on" : ""}`}>
          <input
            type="checkbox"
            checked={isManual}
            onChange={(event) =>
              setServoControlModes((current) => ({
                ...current,
                [modeKey]: event.target.checked ? "manual" : "auto",
              }))
            }
          />
          <span className="ios-toggle-track">
            <span className="servo-mode-rocker-label">{isManual ? "M" : "A"}</span>
          </span>
        </label>
      </div>
    );
  }

  function renderByjModeToggle(modeKey) {
    const isManual = byjControlModes[modeKey] === "manual";
    return (
      <div className="servo-mode-toggle servo-mode-toggle-rocker">
        <label className={`ios-toggle ios-toggle-small servo-mode-rocker ${isManual ? "is-on" : ""}`}>
          <input
            type="checkbox"
            checked={isManual}
            onChange={(event) =>
              setByjControlModes((current) => ({
                ...current,
                [modeKey]: event.target.checked ? "manual" : "auto",
              }))
            }
          />
          <span className="ios-toggle-track">
            <span className="servo-mode-rocker-label">{isManual ? "M" : "A"}</span>
          </span>
        </label>
      </div>
    );
  }

  function renderStripIcon(icon) {
    if (icon === "servo") {
      return (
        <svg viewBox="0 0 24 24" aria-hidden="true">
          <rect x="5.5" y="8" width="13" height="10" rx="2.4" />
          <path d="M8 18v2" />
          <path d="M16 18v2" />
          <path d="M6.5 10H4.5" />
          <path d="M19.5 10h-2" />
          <circle cx="12" cy="7" r="2.1" />
          <path d="M12 4.9v-1.6" />
          <path d="M12 7l5-2.2" />
          <path d="M12 7l-3.6 3.2" />
        </svg>
      );
    }

    if (icon === "control") {
      return (
        <svg viewBox="0 0 24 24" aria-hidden="true">
          <rect x="6.5" y="2.5" width="11" height="19" rx="4.2" />
          <path d="M9.5 8.5h5" />
          <path d="M9.5 12h5" />
          <path d="M9.5 15.5h5" />
          <circle cx="12" cy="8.5" r="0.9" fill="currentColor" stroke="none" />
          <circle cx="12" cy="12" r="0.9" fill="currentColor" stroke="none" />
          <circle cx="12" cy="15.5" r="0.9" fill="currentColor" stroke="none" />
        </svg>
      );
    }

    if (icon === "node") {
      return (
        <svg viewBox="0 0 24 24" aria-hidden="true">
          <rect x="7" y="7" width="10" height="10" rx="2.5" />
          <path d="M4 9h3" />
          <path d="M4 15h3" />
          <path d="M17 9h3" />
          <path d="M17 15h3" />
          <path d="M9 4v3" />
          <path d="M15 4v3" />
          <path d="M9 17v3" />
          <path d="M15 17v3" />
        </svg>
      );
    }

    return (
      <svg viewBox="0 0 24 24" aria-hidden="true">
        <circle cx="12" cy="12" r="8" />
      </svg>
    );
  }

  function renderSwitchPanelCard() {
    if (!page.switchPanel) {
      return null;
    }

    return (
      <article className="tv-control-card">
        <span className="eyebrow">{page.switchPanel.title}</span>
        <div className="switch-panel-list">
          {roomSwitchItems.map((item) => (
            <div key={item.id} className="switch-panel-row">
              <div className="switch-panel-meta">
                <strong>{item.label}</strong>
              </div>
              <label className={`ios-toggle ${item.relayOn ? "is-on" : ""}`}>
                <input
                  type="checkbox"
                  checked={item.relayOn}
                  onChange={(event) =>
                    setSwitchRelay(item.nodeKind, item.relayKey, event.target.checked, item.commandType)
                  }
                />
                <span className="ios-toggle-track" />
              </label>
              {item.hasLedControl ? (
                <div className="switch-led-block">
                  <button
                    className={`switch-led-indicator ${item.ledIsOn ? "is-on" : ""} ${item.ledIsAuto ? "is-auto" : ""} switch-led-mode-${String(item.ledMode ?? "off").replace(/_/g, "-")}`}
                    type="button"
                    onClick={() =>
                      setSwitchLedMode(
                        item.nodeKind,
                        item.relayKey,
                        item.ledIsOn ? "off" : "on",
                      )
                    }
                  />
                  <div className="switch-mode-segment">
                    <button
                      className={item.ledIsAuto ? "active" : ""}
                      type="button"
                      onClick={() => setSwitchLedMode(item.nodeKind, item.relayKey, "auto")}
                    >
                      Auto
                    </button>
                    <button
                      className={!item.ledIsAuto ? "active" : ""}
                      type="button"
                      onClick={() =>
                        setSwitchLedMode(
                          item.nodeKind,
                          item.relayKey,
                          item.ledMode === "off" || item.ledMode === "auto" ? "on" : item.ledMode,
                        )
                      }
                    >
                      Manual
                    </button>
                  </div>
                </div>
              ) : (
                <div />
              )}
              {item.hasLedControl && !item.ledIsAuto ? (
                <div className="switch-led-modes">
                  {item.ledModes
                    .filter((mode) => mode.key !== "auto")
                    .map((mode) => (
                      <button
                        key={`${item.id}-${mode.key}`}
                        className={`source-chip ${item.ledMode === mode.key ? "active" : ""}`}
                        type="button"
                        onClick={() => setSwitchLedMode(item.nodeKind, item.relayKey, mode.key)}
                      >
                        {mode.label}
                      </button>
                    ))}
                </div>
              ) : null}
            </div>
          ))}
        </div>
      </article>
    );
  }

  function setPumpMode(pumpId, mode) {
    setPumpStates((current) => {
      const pump = current[pumpId] ?? {};
      return {
        ...current,
        [pumpId]: {
          ...pump,
          mode,
          state: mode === "manual" ? "off" : "off",
        },
      };
    });
  }

  function setPumpPower(pumpId, state) {
    setPumpStates((current) => {
      const pump = current[pumpId];
      if (!pump || pump.mode !== "manual") {
        return current;
      }
      return {
        ...current,
        [pumpId]: {
          ...pump,
          state,
        },
      };
    });
  }

  async function requestPumpStatus(delayMs = 250) {
    const baseUrl = getPumpBaseUrl();
    if (!baseUrl) {
      return;
    }

    window.setTimeout(async () => {
      try {
        const response = await fetch(`${baseUrl}/status`);
        const payload = await response.json();
        if (!response.ok) {
          throw new Error(payload.error || `status ${response.status}`);
        }
        applyBridgeSnapshotToControls(payload);
      } catch (error) {
        setBridgeError(error.message);
      }
    }, delayMs);
  }

  async function sendPumpCommand(item, commandText, onSuccess) {
    const baseUrl = getPumpBaseUrl();
    if (!baseUrl || pendingAquariumControl) {
      return;
    }

    setPendingAquariumControl(commandText);
    try {
      const response = await fetch(`${baseUrl}/send`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ text: commandText }),
      });
      const payload = await response.json();
      if (!response.ok) {
        throw new Error(payload.error || `status ${response.status}`);
      }
      if (onSuccess) {
        onSuccess();
      }
      if (payload.state) {
        applyBridgeSnapshotToControls(payload.state);
      }
      setBridgeError("");
      requestPumpStatus(200);
    } catch (error) {
      setBridgeError(`${item.label}: ${error.message}`);
    } finally {
      setPendingAquariumControl((current) => (current === commandText ? "" : current));
    }
  }

  function handlePumpModeChange(item, mode) {
    const pump = pumpStates[item.id];
    if (pump?.mode === mode) {
      return;
    }

    sendPumpCommand(item, `pump ${item.key} mode ${mode}`, () => {
      setPumpMode(item.id, mode);
    });
  }

  function handlePumpPowerChange(item, state) {
    const pump = pumpStates[item.id];
    if (!pump || pump.mode !== "manual" || pump.state === state) {
      return;
    }

    sendPumpCommand(item, `pump ${item.key} ${state}`, () => {
      setPumpPower(item.id, state);
    });
  }

  function setMiscMode(itemId, mode) {
    setMiscStates((current) => {
      const item = current[itemId] ?? {};
      return {
        ...current,
        [itemId]: {
          ...item,
          mode,
          state: mode === "manual" ? "off" : "off",
        },
      };
    });
  }

  function setMiscPower(itemId, state) {
    setMiscStates((current) => {
      const item = current[itemId];
      if (!item || item.mode !== "manual") {
        return current;
      }
      return {
        ...current,
        [itemId]: {
          ...item,
          state,
        },
      };
    });
  }

  function handleMiscModeChange(item, mode) {
    const misc = miscStates[item.id];
    if (misc?.mode === mode) {
      return;
    }

    sendPumpCommand(item, `misc ${item.key} mode ${mode}`, () => {
      setMiscMode(item.id, mode);
    });
  }

  function handleMiscPowerChange(item, state) {
    const misc = miscStates[item.id];
    if (!misc || misc.mode !== "manual" || misc.state === state) {
      return;
    }

    sendPumpCommand(item, `misc ${item.key} ${state}`, () => {
      setMiscPower(item.id, state);
    });
  }

  useEffect(() => {
    if (!page.pumpControl?.apiPath) {
      return;
    }

    requestPumpStatus(0);
    const timer = window.setInterval(() => {
      requestPumpStatus(0);
    }, 10000);

    return () => {
      window.clearInterval(timer);
    };
  }, [page.pumpControl?.apiPath]);

  return (
    <>
      {page.featuredDevice || page.hideHero ? null : (
        <section className="module-hero-card">
          <div className="module-hero-layout">
            <div>
              <span className="eyebrow">{page.eyebrow}</span>
              <h2 className="module-title">{page.title}</h2>
              <p className="module-description">{page.description}</p>
            </div>

            <div className="module-status-strip">
              <article>
                <span>Connectivity</span>
                <strong>Stable</strong>
              </article>
            </div>
          </div>
        </section>
      )}

      {page.featuredDevice || page.roomNode || page.roomNodeSecondary ? (
        <section className="room-device-strip" aria-label="Room devices">
          {page.featuredDevice ? (
            <button
              className={`room-device-icon ${showFeaturedDeviceTab ? "active" : ""}`}
              type="button"
              aria-label={page.featuredDevice.name}
              title={page.featuredDevice.name}
              onClick={() => setActiveRoomDeviceTab("featured")}
            >
              <svg viewBox="0 0 24 24" aria-hidden="true">
                <rect x="3" y="5" width="18" height="12" rx="2.5" />
                <path d="M9 20h6" />
                <path d="M12 17v3" />
              </svg>
            </button>
          ) : null}
          {page.switchPanel ? (
            <button
              className={`room-device-icon ${showSwitchPanelTab ? "active" : ""}`}
              type="button"
              aria-label={page.switchPanel.title}
              title={page.switchPanel.title}
              onClick={() => setActiveRoomDeviceTab("switch-panel")}
            >
              {renderStripIcon("control")}
            </button>
          ) : null}
          {page.roomNode ? (
            <button
              className={`room-device-icon ${showRoomNodeTab ? "active" : ""}`}
              type="button"
              aria-label={page.roomNode.title}
              title={page.roomNode.title}
              onClick={() => setActiveRoomDeviceTab("room-node")}
            >
              {renderStripIcon("node")}
            </button>
          ) : null}
          {page.roomNodeSecondary ? (
            <button
              className={`room-device-icon ${showRoomNodeSecondaryTab ? "active" : ""}`}
              type="button"
              aria-label={page.roomNodeSecondary.title}
              title={page.roomNodeSecondary.title}
              onClick={() => setActiveRoomDeviceTab("room-node-secondary")}
            >
              {renderStripIcon("node")}
            </button>
          ) : null}
        </section>
      ) : null}

      {!page.featuredDevice && aquariumSectionTabs.length ? (
        <section className="room-device-strip" aria-label="Module sections">
          {aquariumSectionTabs.map((item) => (
            <button
              key={item.id}
              className={`room-device-icon ${activeModuleTab === item.id ? "active" : ""}`}
              type="button"
              aria-label={item.label}
              title={item.label}
              onClick={() => {
                if (hasAquariumTabs) {
                  setActiveModuleTab(item.id);
                  return;
                }
                focusPageSection(item.id);
              }}
            >
              {renderStripIcon(item.icon)}
            </button>
          ))}
        </section>
      ) : null}

      {page.highlights?.length && showControlTab ? (
        <section className="module-grid">
          {page.highlights.map((group) => (
            <article
              key={group.id || group.title}
              ref={(element) => {
                if (group.id) {
                  sectionRefs.current[group.id] = element;
                }
              }}
              className={`module-card ${group.id === "control" ? "module-card-wide module-card-plain" : ""}`}
            >
              {group.id === "control" ? null : <span className="eyebrow">{group.title}</span>}
              {group.id === "control" && page.pumpControl ? (
                <div className="pump-control-stack">
                  {page.waterLevelSensors ? (
                    <article className="pump-control-card sensor-control-card">
                      <span className="eyebrow">{page.waterLevelSensors.title}</span>
                      <div className="sensor-chart-grid">
                        {groupedWaterSensors.map((group) => (
                          <div key={group.name} className="sensor-chart-card">
                            <div className="sensor-chart-bar">
                              {["low", "normal", "high"].map((levelKey) => {
                                const level = group.levels[levelKey];
                                if (!level) {
                                  return (
                                    <div
                                      key={`${group.name}-${levelKey}`}
                                      className="sensor-chart-segment sensor-chart-segment-empty"
                                      aria-hidden="true"
                                    />
                                  );
                                }
                                return (
                                  <div
                                    key={level.id}
                                    className={`sensor-chart-segment ${(level.wet ?? false) ? "wet" : "dry"}`}
                                  >
                                    <span className="sensor-chart-level">{level.label}</span>
                                    <span className="sensor-chart-state">{(level.wet ?? false) ? "Wet" : "Dry"}</span>
                                  </div>
                                );
                              })}
                            </div>
                            <div className="sensor-chart-name">{group.name}</div>
                          </div>
                        ))}
                      </div>
                    </article>
                  ) : null}
                  {page.temperatureSensors ? (
                    <article className="pump-control-card temperature-control-card">
                      <span className="eyebrow">{page.temperatureSensors.title}</span>
                      <div className="temperature-grid">
                        {temperatureReadings.map((item) => {
                          const hasValue = Number.isFinite(item.celsius);
                          return (
                            <div key={item.id} className="temperature-card">
                              <span className="temperature-label">{item.label}</span>
                              <span className="temperature-value">
                                {hasValue ? `${item.celsius.toFixed(1)}°C` : "--"}
                              </span>
                              <span className="temperature-raw">
                                {Number.isFinite(item.raw) ? `ADC ${item.raw}` : "ADC --"}
                              </span>
                            </div>
                          );
                        })}
                      </div>
                    </article>
                  ) : null}
                  <article className="pump-control-card">
                    <span className="eyebrow">{page.pumpControl.title}</span>
                    <div className="pump-list">
                      {page.pumpControl.items.map((item) => {
                        const pump = pumpStates[item.id] ?? { mode: item.mode ?? "auto", state: item.state ?? "off" };
                        const isManual = pump.mode === "manual";
                        const controlsPending = Boolean(pendingAquariumControl);
                          return (
                            <div key={item.id} className="pump-row">
                              <div className="pump-copy">
                                <strong>{item.label}</strong>
                              </div>
                              <div className="pump-actions">
                              <div className="pump-mode-toggle">
                                <button
                                  className={`pump-toggle-button ${pump.mode === "auto" ? "active" : ""}`}
                                  type="button"
                                  disabled={controlsPending}
                                  onClick={() => handlePumpModeChange(item, "auto")}
                                >
                                  Auto
                                </button>
                                <button
                                  className={`pump-toggle-button ${pump.mode === "manual" ? "active" : ""}`}
                                  type="button"
                                  disabled={controlsPending}
                                  onClick={() => handlePumpModeChange(item, "manual")}
                                >
                                  Manual
                                </button>
                              </div>
                              <div className="pump-power-group">
                                <button
                                  className={`pump-power-button on ${pump.state === "on" ? "active" : ""}`}
                                  type="button"
                                  disabled={!isManual || controlsPending}
                                  onClick={() => handlePumpPowerChange(item, "on")}
                                >
                                  On
                                </button>
                                <button
                                  className={`pump-power-button off ${pump.state === "off" ? "active" : ""}`}
                                  type="button"
                                  disabled={!isManual || controlsPending}
                                  onClick={() => handlePumpPowerChange(item, "off")}
                                >
                                  Off
                                </button>
                              </div>
                            </div>
                          </div>
                        );
                      })}
                    </div>
                  </article>
                  {page.miscControl ? (
                    <article className="pump-control-card">
                      <span className="eyebrow">{page.miscControl.title}</span>
                      <div className="pump-list">
                        {page.miscControl.items.map((item) => {
                          const misc = miscStates[item.id] ?? { mode: item.mode ?? "auto", state: item.state ?? "off" };
                          const isManual = misc.mode === "manual";
                          const controlsPending = Boolean(pendingAquariumControl);
                          return (
                            <div key={item.id} className="pump-row">
                              <div className="pump-copy">
                                <strong>{item.label}</strong>
                              </div>
                              <div className="pump-actions">
                                <div className="pump-mode-toggle">
                                  <button
                                    className={`pump-toggle-button ${misc.mode === "auto" ? "active" : ""}`}
                                    type="button"
                                    disabled={controlsPending}
                                    onClick={() => handleMiscModeChange(item, "auto")}
                                  >
                                    Auto
                                  </button>
                                  <button
                                    className={`pump-toggle-button ${misc.mode === "manual" ? "active" : ""}`}
                                    type="button"
                                    disabled={controlsPending}
                                    onClick={() => handleMiscModeChange(item, "manual")}
                                  >
                                    Manual
                                  </button>
                                </div>
                                <div className="pump-power-group">
                                  <button
                                    className={`pump-power-button on ${misc.state === "on" ? "active" : ""}`}
                                    type="button"
                                    disabled={!isManual || controlsPending}
                                    onClick={() => handleMiscPowerChange(item, "on")}
                                  >
                                    On
                                  </button>
                                  <button
                                    className={`pump-power-button off ${misc.state === "off" ? "active" : ""}`}
                                    type="button"
                                    disabled={!isManual || controlsPending}
                                    onClick={() => handleMiscPowerChange(item, "off")}
                                  >
                                    Off
                                  </button>
                                </div>
                              </div>
                            </div>
                          );
                        })}
                      </div>
                    </article>
                  ) : null}
                </div>
              ) : (
                <div className="bullet-list">
                  {group.items.map((item) => (
                    <div key={item} className="bullet-row">
                      <span className="bullet-dot" aria-hidden="true" />
                      <p>{item}</p>
                    </div>
                  ))}
                </div>
              )}
            </article>
          ))}
        </section>
      ) : null}

      {page.featuredDevice ? (
        <>
          {showFeaturedDeviceTab ? (
            <>
              <section ref={featuredDeviceCardRef} className="tv-featured-shell">
                <div className="tv-featured-copy">
                  <h3>{page.featuredDevice.name}</h3>
                </div>

                <div className="tv-featured-actions">
                  <div className="tv-featured-status-column">
                    <article className="tv-control-card featured-status-card">
                      <span className="eyebrow">Status</span>
                      <div className="tv-status-grid">
                        <div className="tv-status-item">
                          <span>Running App</span>
                          <strong>{currentForegroundAppTitle}</strong>
                        </div>
                        <div className="tv-status-item">
                          <span>Started At</span>
                          <strong>{formatDateTime(currentForegroundAppStartedAt)}</strong>
                        </div>
                        <div className="tv-status-item">
                          <span>Run Time</span>
                          <strong>{formatDurationMinutes(currentForegroundAppDurationSeconds)} min</strong>
                        </div>
                      </div>
                    </article>
                  </div>

                  <div className="tv-control-card inline-remote-card">
                    <span className="eyebrow">Remote Pad</span>
                    <div className="remote-shell">
                      <div className="magic-remote-utility-grid">
                        {[
                          {
                            label: canPowerOn ? "Turn TV on" : "Turn TV off",
                            className: `power-icon-button ${canPowerOn ? "power-off" : "power-on"} ${
                              pendingPowerCommand === "turn_on"
                                ? "pending-on"
                                : pendingPowerCommand === "turn_off"
                                  ? "pending-off"
                                  : ""
                            }`,
                            command: { command: canPowerOn ? "turn_on" : "turn_off" },
                            icon: renderRemoteUtilityIcon("Power"),
                          },
                          {
                            label: "Mute",
                            className: "remote-key remote-key-icon",
                            command: { command: "toggle_mute" },
                            icon: renderRemoteUtilityIcon("Mute"),
                          },
                          {
                            label: "Source",
                            className: "remote-key remote-key-icon",
                            command: { command: "show_input_picker" },
                            icon: renderRemoteUtilityIcon("Source"),
                          },
                          {
                            label: "Settings",
                            className: "remote-key remote-key-icon",
                            command: { command: "remote_button", button: "MENU" },
                            icon: renderRemoteUtilityIcon("Settings"),
                          },
                          {
                            label: "Home",
                            className: "remote-key remote-key-icon",
                            command: { command: "remote_button", button: "HOME" },
                            icon: renderRemoteUtilityIcon("Home"),
                          },
                          {
                            label: "Back",
                            className: "remote-key remote-key-icon",
                            command: { command: "remote_button", button: "BACK" },
                            icon: renderRemoteUtilityIcon("Back"),
                          },
                          { label: "Vol -", className: "remote-key remote-key-pill", command: { command: "volume_down" } },
                          { label: "Vol +", className: "remote-key remote-key-pill", command: { command: "volume_up" } },
                        ].map((item) => (
                          <button
                            key={item.label}
                            className={item.className}
                            type="button"
                            aria-label={item.label}
                            title={item.label}
                            onClick={() => sendFeaturedCommand(item.command)}
                          >
                            {item.icon ?? item.label}
                          </button>
                        ))}
                      </div>

                      <div className="remote-dpad">
                        {["Up", "Left", "OK", "Right", "Down"].map((action) => (
                          <button
                            key={action}
                            className={`remote-key remote-key-${action.toLowerCase()}`}
                            type="button"
                            onClick={() => {
                              const buttonMap = {
                                OK: "ENTER",
                                "CH+": "CHANNELUP",
                                "CH-": "CHANNELDOWN",
                              };
                              sendFeaturedCommand({
                                command: "remote_button",
                                button: buttonMap[action] || action,
                              });
                            }}
                          >
                            {action}
                          </button>
                        ))}
                      </div>

                      <div className="remote-channel-row">
                        {[
                          { label: "CH-", button: "CHANNELDOWN" },
                          { label: "CH+", button: "CHANNELUP" },
                        ].map((item) => (
                          <button
                            key={item.label}
                            className="remote-key remote-key-wide"
                            type="button"
                            onClick={() =>
                              sendFeaturedCommand({
                                command: "remote_button",
                                button: item.button,
                              })
                            }
                          >
                            {item.label}
                          </button>
                        ))}
                      </div>
                    </div>
                    <div className={`volume-hud outside ${showVolumeHud ? "visible" : ""}`} aria-hidden={!showVolumeHud}>
                      <div
                        ref={volumeHudTrackRef}
                        className={`volume-hud-track ${volumeHudDragging ? "dragging" : ""}`}
                        role="slider"
                        aria-label="TV volume"
                        aria-valuemin={0}
                        aria-valuemax={100}
                        aria-valuenow={clampVolume(volumeHudValue)}
                        tabIndex={0}
                        onPointerDown={handleVolumePointerDown}
                        onPointerMove={handleVolumePointerMove}
                        onPointerUp={finishVolumePointerInteraction}
                        onPointerCancel={finishVolumePointerInteraction}
                      >
                        <div
                          className="volume-hud-fill"
                          style={{ height: `${clampVolume(volumeHudValue)}%` }}
                        />
                      </div>
                      <span>{clampVolume(volumeHudValue)}</span>
                    </div>
                  </div>
                </div>
              </section>

              <section className="tv-secondary-grid">
                <article className="tv-control-card">
                  <span className="eyebrow">Quick Apps</span>
                  <div className="tv-app-grid">
                    {featuredLaunchApps.map((app) => (
                      <button
                        key={app.id}
                        className="tv-app-button"
                        type="button"
                        title={app.title}
                        onClick={() => sendFeaturedCommand({ command: "launch_app", appId: app.id })}
                      >
                        {preferredLaunchAppIconMap[app.id] || app.icon ? (
                          <img
                            className="tv-app-icon"
                            src={preferredLaunchAppIconMap[app.id] || app.icon}
                            alt=""
                            loading="lazy"
                          />
                        ) : (
                          <span className="tv-app-fallback">{String(app.title || app.id || "?").slice(0, 1)}</span>
                        )}
                        <span className="tv-app-label">{app.title ?? app.id}</span>
                      </button>
                    ))}
                  </div>
                </article>

                <article className="tv-control-card">
                  <span className="eyebrow">App History</span>
                  {deviceAppHistoryError ? <p>{deviceAppHistoryError}</p> : null}
                  <div className="tv-app-history-filters">
                    <input
                      type="text"
                      value={deviceAppHistoryNameFilter}
                      placeholder="Filter by app name"
                      onChange={(event) => setDeviceAppHistoryNameFilter(event.target.value)}
                    />
                    <input
                      type="date"
                      value={deviceAppHistoryDateFilter}
                      onChange={(event) => setDeviceAppHistoryDateFilter(event.target.value)}
                    />
                  </div>
                  <div className="tv-app-history-table-wrap">
                    <table className="tv-app-history-table">
                      <thead>
                        <tr>
                          <th>No.</th>
                          <th>App</th>
                          <th>Start</th>
                          <th>End</th>
                          <th>Duration</th>
                        </tr>
                      </thead>
                      <tbody>
                        {filteredDeviceAppHistory.map((item, index) => {
                          const payload = item.payloadJson ?? {};
                          return (
                            <tr key={item.id}>
                              <td>{index + 1}</td>
                              <td>{payload.title ?? payload.appId ?? "Unknown"}</td>
                              <td>{formatDateTime(payload.startedAt)}</td>
                              <td>{formatDateTime(payload.endedAt)}</td>
                              <td>{formatDurationMinutes(payload.durationSeconds)} min</td>
                            </tr>
                          );
                        })}
                      </tbody>
                    </table>
                  </div>
                </article>
              </section>
            </>
          ) : null}

          {page.switchPanel && showSwitchPanelTab ? (
            <section className="tv-control-grid">
              {renderSwitchPanelCard()}
            </section>
          ) : null}

          {(showRoomNodeTab || showRoomNodeSecondaryTab) && (page.roomNode || page.roomNodeSecondary) ? (
            <section className="tv-control-grid">
            {page.roomNode && showRoomNodeTab ? (
              <article ref={roomNodeCardRef} className="tv-control-card">
                <span className="eyebrow">{page.roomNode.title}</span>
                <strong>{liveRoomNodeConnectivity}</strong>
                <p>
                  {roomNodeError
                    ? `Node error: ${roomNodeError}`
                    : roomNodeState?.lastError
                      ? roomNodeState.lastError
                      : roomNodeState?.ip
                        ? `IP ${roomNodeState.ip} / RSSI ${roomNodeState.wifiRssi ?? "n/a"}`
                        : "Waiting for node state"}
                </p>
                <div className="next-step-grid">
                  {(page.roomNode.relays ?? []).map((relay) => {
                    const relayOn = Boolean(roomNodeState?.relays?.[relay.key]);
                    const relayLedMode =
                      roomNodeState?.ledModes?.[relay.key] ?? roomNodeState?.ledMode ?? "unknown";
                    return (
                      <div key={relay.key} className="next-step-card">
                        <span>{relay.label}</span>
                        <strong>{relayOn ? "On" : "Off"}</strong>
                        <div className="chip-grid">
                          <button
                            className="source-chip"
                            type="button"
                            onClick={() =>
                              sendRoomNodeCommand({
                                action: "set_relay",
                                channel: relay.key,
                                value: !relayOn,
                              })
                            }
                          >
                            {relayOn ? "Turn Off" : "Turn On"}
                          </button>
                        </div>
                        <div className="room-node-led-mode room-node-led-mode-compact">
                          <div className="room-node-led-mode-head">
                            <span>LED Mode</span>
                            <strong>{relayLedMode}</strong>
                          </div>
                          <div className="chip-grid">
                            {(page.roomNode.ledModes ?? []).map((mode) => (
                              <button
                                key={`${relay.key}-${mode.key}`}
                                className={`source-chip ${relayLedMode === mode.key ? "active" : ""}`}
                                type="button"
                                onClick={() =>
                                  sendRoomNodeCommand({
                                    action: "set_led_mode",
                                    ...(page.roomNode.ledModeScopedByRelay === false
                                      ? {}
                                      : { channel: relay.key }),
                                    mode: mode.key,
                                  })
                                }
                              >
                                {mode.label}
                              </button>
                            ))}
                          </div>
                        </div>
                      </div>
                    );
                  })}
                  {(page.roomNode.touches ?? []).map((touch) => (
                    <div key={touch.key} className="next-step-card">
                      <span>{touch.label}</span>
                      <strong>{roomNodeState?.touches?.[touch.key] ? "Touched" : "Idle"}</strong>
                    </div>
                  ))}
                </div>
              </article>
            ) : null}
            {page.roomNodeSecondary && showRoomNodeSecondaryTab ? (
              <article ref={roomNodeSecondaryCardRef} className="tv-control-card">
                <span className="eyebrow">{page.roomNodeSecondary.title}</span>
                <strong>{liveRoomNodeSecondaryConnectivity}</strong>
                <p>
                  {roomNodeSecondaryError
                    ? `Node error: ${roomNodeSecondaryError}`
                    : roomNodeSecondaryState?.lastError
                      ? roomNodeSecondaryState.lastError
                      : roomNodeSecondaryState?.ip
                        ? `IP ${roomNodeSecondaryState.ip} / RSSI ${roomNodeSecondaryState.wifiRssi ?? "n/a"}`
                        : "Waiting for node state"}
                </p>
                {(page.roomNodeSecondary.ledModes ?? []).length > 0 ? (
                  <div className="room-node-led-mode">
                    <div className="room-node-led-mode-head">
                      <span>LED Mode</span>
                      <strong>{roomNodeSecondaryState?.ledMode ?? "unknown"}</strong>
                    </div>
                    <div className="chip-grid">
                      {(page.roomNodeSecondary.ledModes ?? []).map((mode) => (
                        <button
                          key={mode.key}
                          className={`source-chip ${
                            roomNodeSecondaryState?.ledMode === mode.key ? "active" : ""
                          }`}
                          type="button"
                          onClick={() =>
                            sendRoomNodeSecondaryCommand({
                              action: "set_led_mode",
                              mode: mode.key,
                            })
                          }
                        >
                          {mode.label}
                        </button>
                      ))}
                    </div>
                  </div>
                ) : null}
                <div className="next-step-grid">
                  {page.roomNodeSecondary.remoteRelayLabel ? (
                    <div className="next-step-card">
                      <span>{page.roomNodeSecondary.remoteRelayLabel}</span>
                      <strong>{roomNodeSecondaryState?.remoteRelay ? "On" : "Off"}</strong>
                      <div className="chip-grid">
                        {page.roomNodeSecondary.remoteRelayToggleAction ? (
                          <button
                            className="source-chip"
                            type="button"
                            onClick={() =>
                              sendRoomNodeSecondaryCommand(page.roomNodeSecondary.remoteRelayToggleAction)
                            }
                          >
                            Toggle
                          </button>
                        ) : null}
                        {page.roomNodeSecondary.remoteRelaySyncAction ? (
                          <button
                            className="source-chip"
                            type="button"
                            onClick={() =>
                              sendRoomNodeSecondaryCommand(page.roomNodeSecondary.remoteRelaySyncAction)
                            }
                          >
                            Sync
                          </button>
                        ) : null}
                      </div>
                    </div>
                  ) : null}
                  {(page.roomNodeSecondary.relays ?? []).map((relay) => {
                    const relayOn = Boolean(roomNodeSecondaryState?.relays?.[relay.key]);
                    return (
                      <div key={relay.key} className="next-step-card">
                        <span>{relay.label}</span>
                        <strong>{relayOn ? "On" : "Off"}</strong>
                        <div className="chip-grid">
                          <button
                            className="source-chip"
                            type="button"
                            onClick={() =>
                              sendRoomNodeSecondaryCommand({
                                action: "set_relay",
                                value: !relayOn,
                              })
                            }
                          >
                            {relayOn ? "Turn Off" : "Turn On"}
                          </button>
                        </div>
                      </div>
                    );
                  })}
                  {(page.roomNodeSecondary.touches ?? []).map((touch) => (
                    <div key={touch.key} className="next-step-card">
                      <span>{touch.label}</span>
                      <strong>{roomNodeSecondaryState?.touches?.[touch.key] ? "Touched" : "Idle"}</strong>
                    </div>
                  ))}
                </div>
              </article>
            ) : null}
            </section>
          ) : null}
        </>
      ) : null}

      {!page.featuredDevice && (page.roomNode || page.roomNodeSecondary) ? (
        <section className="tv-control-grid">
          {page.switchPanel ? renderSwitchPanelCard() : null}
          {page.roomNode ? (
            <article ref={roomNodeCardRef} className="tv-control-card">
              <span className="eyebrow">{page.roomNode.title}</span>
              <strong>{liveRoomNodeConnectivity}</strong>
              <p>
                {roomNodeError
                  ? `Node error: ${roomNodeError}`
                  : roomNodeState?.lastError
                    ? roomNodeState.lastError
                    : roomNodeState?.ip
                      ? `IP ${roomNodeState.ip} / RSSI ${roomNodeState.wifiRssi ?? "n/a"}`
                      : "Waiting for node state"}
              </p>
              <div className="next-step-grid">
                {(page.roomNode.relays ?? []).map((relay) => {
                  const relayOn = Boolean(roomNodeState?.relays?.[relay.key]);
                  const relayLedMode =
                    roomNodeState?.ledModes?.[relay.key] ?? roomNodeState?.ledMode ?? "unknown";
                  return (
                    <div key={relay.key} className="next-step-card">
                      <span>{relay.label}</span>
                      <strong>{relayOn ? "On" : "Off"}</strong>
                      <div className="chip-grid">
                        <button
                          className="source-chip"
                          type="button"
                          onClick={() =>
                            sendRoomNodeCommand({
                              action: "set_relay",
                              channel: relay.key,
                              value: !relayOn,
                            })
                          }
                        >
                          {relayOn ? "Turn Off" : "Turn On"}
                        </button>
                      </div>
                      <div className="room-node-led-mode room-node-led-mode-compact">
                        <div className="room-node-led-mode-head">
                          <span>LED Mode</span>
                          <strong>{relayLedMode}</strong>
                        </div>
                        <div className="chip-grid">
                          {(page.roomNode.ledModes ?? []).map((mode) => (
                            <button
                              key={`${relay.key}-${mode.key}`}
                              className={`source-chip ${relayLedMode === mode.key ? "active" : ""}`}
                              type="button"
                              onClick={() =>
                                sendRoomNodeCommand({
                                  action: "set_led_mode",
                                  ...(page.roomNode.ledModeScopedByRelay === false
                                    ? {}
                                    : { channel: relay.key }),
                                  mode: mode.key,
                                })
                              }
                            >
                              {mode.label}
                            </button>
                          ))}
                        </div>
                      </div>
                    </div>
                  );
                })}
                {(page.roomNode.touches ?? []).map((touch) => (
                  <div key={touch.key} className="next-step-card">
                    <span>{touch.label}</span>
                    <strong>{roomNodeState?.touches?.[touch.key] ? "Touched" : "Idle"}</strong>
                  </div>
                ))}
              </div>
            </article>
          ) : null}
          {page.roomNodeSecondary ? (
            <article ref={roomNodeSecondaryCardRef} className="tv-control-card">
              <span className="eyebrow">{page.roomNodeSecondary.title}</span>
              <strong>{liveRoomNodeSecondaryConnectivity}</strong>
              <p>
                {roomNodeSecondaryError
                  ? `Node error: ${roomNodeSecondaryError}`
                  : roomNodeSecondaryState?.lastError
                    ? roomNodeSecondaryState.lastError
                    : roomNodeSecondaryState?.ip
                      ? `IP ${roomNodeSecondaryState.ip} / RSSI ${roomNodeSecondaryState.wifiRssi ?? "n/a"}`
                      : "Waiting for node state"}
              </p>
              {(page.roomNodeSecondary.ledModes ?? []).length > 0 ? (
                <div className="room-node-led-mode">
                  <div className="room-node-led-mode-head">
                    <span>LED Mode</span>
                    <strong>{roomNodeSecondaryState?.ledMode ?? "unknown"}</strong>
                  </div>
                  <div className="chip-grid">
                    {(page.roomNodeSecondary.ledModes ?? []).map((mode) => (
                      <button
                        key={mode.key}
                        className={`source-chip ${
                          roomNodeSecondaryState?.ledMode === mode.key ? "active" : ""
                        }`}
                        type="button"
                        onClick={() =>
                          sendRoomNodeSecondaryCommand({
                            action: "set_led_mode",
                            mode: mode.key,
                          })
                        }
                      >
                        {mode.label}
                      </button>
                    ))}
                  </div>
                </div>
              ) : null}
              <div className="next-step-grid">
                {page.roomNodeSecondary.remoteRelayLabel ? (
                  <div className="next-step-card">
                    <span>{page.roomNodeSecondary.remoteRelayLabel}</span>
                    <strong>{roomNodeSecondaryState?.remoteRelay ? "On" : "Off"}</strong>
                    <div className="chip-grid">
                      {page.roomNodeSecondary.remoteRelayToggleAction ? (
                        <button
                          className="source-chip"
                          type="button"
                          onClick={() =>
                            sendRoomNodeSecondaryCommand(page.roomNodeSecondary.remoteRelayToggleAction)
                          }
                        >
                          Toggle
                        </button>
                      ) : null}
                      {page.roomNodeSecondary.remoteRelaySyncAction ? (
                        <button
                          className="source-chip"
                          type="button"
                          onClick={() =>
                            sendRoomNodeSecondaryCommand(page.roomNodeSecondary.remoteRelaySyncAction)
                          }
                        >
                          Sync
                        </button>
                      ) : null}
                    </div>
                  </div>
                ) : null}
                {(page.roomNodeSecondary.relays ?? []).map((relay) => {
                  const relayOn = Boolean(roomNodeSecondaryState?.relays?.[relay.key]);
                  return (
                    <div key={relay.key} className="next-step-card">
                      <span>{relay.label}</span>
                      <strong>{relayOn ? "On" : "Off"}</strong>
                      <div className="chip-grid">
                        <button
                          className="source-chip"
                          type="button"
                          onClick={() =>
                            sendRoomNodeSecondaryCommand({
                              action: "set_relay",
                              value: !relayOn,
                            })
                          }
                        >
                          {relayOn ? "Turn Off" : "Turn On"}
                        </button>
                      </div>
                    </div>
                  );
                })}
                {(page.roomNodeSecondary.touches ?? []).map((touch) => (
                  <div key={touch.key} className="next-step-card">
                    <span>{touch.label}</span>
                    <strong>{roomNodeSecondaryState?.touches?.[touch.key] ? "Touched" : "Idle"}</strong>
                  </div>
                ))}
              </div>
            </article>
          ) : null}
        </section>
      ) : null}

      {page.pinoutsCard ? (
        <section
          ref={(element) => {
            sectionRefs.current.pinouts = element;
          }}
          className="featured-device-card"
        >
          <div className="featured-device-copy">
            <h3>{page.pinoutsCard.title}</h3>
          </div>

          <div className="featured-device-facts">
            {page.pinoutsCard.sections.map((section) => (
              <article key={section.title} className="featured-fact-card">
                <span>{section.title}</span>
                <div className="bullet-list">
                  {section.items.map((item) => (
                    <div key={item} className="bullet-row">
                      <span className="bullet-dot" aria-hidden="true" />
                      <p>{item}</p>
                    </div>
                  ))}
                </div>
              </article>
            ))}
          </div>
        </section>
      ) : null}

      {renderUploadPanel()}

      {page.bridge && (showControlTab || showServoTab || !hasAquariumTabs) ? (
        <section className="tv-control-grid">
          {showServoTab || !hasAquariumTabs ? (
          <div className="volume-card servo-control-card servo-control-card-compact">
              <div className="servo-dashboard-grid">
                {(() => {
                  const fan1Servo = servoDefinitions.find((servo) => servo.key === "fan1");
                  const fan2Servo = servoDefinitions.find((servo) => servo.key === "fan2");
                  const pan1Servo = servoDefinitions.find((servo) => servo.key === "pan1");
                  const pan2Servo = servoDefinitions.find((servo) => servo.key === "pan2");
                  const otherServos = servoDefinitions.filter(
                    (servo) => !["fan1", "fan2", "pan1", "pan2"].includes(servo.key),
                  );
                  const fan1State = fan1Servo ? servoStates[fan1Servo.key] : null;
                  const fan2State = fan2Servo ? servoStates[fan2Servo.key] : null;
                  const pan1State = pan1Servo ? servoStates[pan1Servo.key] : null;
                  const pan2State = pan2Servo ? servoStates[pan2Servo.key] : null;
                  const fan1Angle = fan1Servo
                    ? (() => {
                        const parsed = Number.parseInt(servoAngleInputs[fan1Servo.key] ?? "", 10);
                        return Number.isFinite(parsed)
                          ? clampServoDialValue(parsed, fan1Servo.min, fan1Servo.max)
                          : clampServoDialValue(
                              Number.isFinite(fan1State?.angle) ? fan1State.angle : 90,
                              fan1Servo.min,
                              fan1Servo.max,
                            );
                      })()
                    : 0;
                  const fan2Angle = fan2Servo
                    ? (() => {
                        const parsed = Number.parseInt(servoAngleInputs[fan2Servo.key] ?? "", 10);
                        return Number.isFinite(parsed)
                          ? clampServoDialValue(parsed, fan2Servo.min, fan2Servo.max)
                          : clampServoDialValue(
                              Number.isFinite(fan2State?.angle) ? fan2State.angle : 90,
                              fan2Servo.min,
                              fan2Servo.max,
                            );
                      })()
                    : 0;
                  const pan1Angle = pan1Servo
                    ? (() => {
                        const parsed = Number.parseInt(servoAngleInputs[pan1Servo.key] ?? "", 10);
                        return Number.isFinite(parsed)
                          ? clampServoDialValue(parsed, pan1Servo.min, pan1Servo.max)
                          : clampServoDialValue(
                              Number.isFinite(pan1State?.angle) ? pan1State.angle : 90,
                              pan1Servo.min,
                              pan1Servo.max,
                            );
                      })()
                    : 0;
                  const pan2Angle = pan2Servo
                    ? (() => {
                        const parsed = Number.parseInt(servoAngleInputs[pan2Servo.key] ?? "", 10);
                        return Number.isFinite(parsed)
                          ? clampServoDialValue(parsed, pan2Servo.min, pan2Servo.max)
                          : clampServoDialValue(
                              Number.isFinite(pan2State?.angle) ? pan2State.angle : 90,
                              pan2Servo.min,
                              pan2Servo.max,
                            );
                      })()
                    : 0;

                  return (
                    <>
                      {fan1Servo && fan2Servo ? (
                        <article className="next-step-card servo-dashboard-card servo-dashboard-card-composite">
                          <div className="servo-inline-row servo-inline-row-double">
                            <div className="servo-inline-heading">
                              <div className="servo-dashboard-title servo-inline-title">
                                <span>Fan Wings</span>
                              </div>
                              {renderServoModeToggle("fans")}
                            </div>
                            <div className="servo-inline-split servo-inline-split-two">
                              <div className="servo-inline-split-card">
                                <strong className="servo-inline-split-title">Wing 1</strong>
                                <div className="servo-inline-readout">
                                  <strong>{Number.isFinite(fan1State?.angle) ? `${fan1State.angle}°` : "-"}</strong>
                                </div>
                                <label className="b-axis-tune-field servo-dashboard-field servo-inline-field">
                                  <input
                                    type="number"
                                    min={fan1Servo.min}
                                    max={fan1Servo.max}
                                    step="1"
                                    placeholder="deg"
                                    value={servoAngleInputs[fan1Servo.key] ?? ""}
                                    onChange={(event) => updateServoAngleInput(fan1Servo.key, event.target.value)}
                                    disabled={bAxisCommandPending || servoControlModes.fans !== "manual"}
                                  />
                                </label>
                                <button
                                  className="ghost-pill servo-dashboard-apply"
                                  type="button"
                                  onClick={() => applyServoAngleCommand(fan1Servo.key, fan1Angle)}
                                  disabled={bAxisCommandPending || servoControlModes.fans !== "manual"}
                                >
                                  Apply
                                </button>
                              </div>
                              <div className="servo-inline-split-card">
                                <strong className="servo-inline-split-title">Wing 2</strong>
                                <div className="servo-inline-readout">
                                  <strong>{Number.isFinite(fan2State?.angle) ? `${fan2State.angle}°` : "-"}</strong>
                                </div>
                                <label className="b-axis-tune-field servo-dashboard-field servo-inline-field">
                                  <input
                                    type="number"
                                    min={fan2Servo.min}
                                    max={fan2Servo.max}
                                    step="1"
                                    placeholder="deg"
                                    value={servoAngleInputs[fan2Servo.key] ?? ""}
                                    onChange={(event) => updateServoAngleInput(fan2Servo.key, event.target.value)}
                                    disabled={bAxisCommandPending || servoControlModes.fans !== "manual"}
                                  />
                                </label>
                                <button
                                  className="ghost-pill servo-dashboard-apply"
                                  type="button"
                                  onClick={() => applyServoAngleCommand(fan2Servo.key, fan2Angle)}
                                  disabled={bAxisCommandPending || servoControlModes.fans !== "manual"}
                                >
                                  Apply
                                </button>
                              </div>
                            </div>
                          </div>
                        </article>
                      ) : null}

                      {pan1Servo && pan2Servo ? (
                        <article className="next-step-card servo-dashboard-card servo-dashboard-card-composite">
                          <div className="servo-inline-row servo-inline-row-double">
                            <div className="servo-inline-heading">
                              <div className="servo-dashboard-title servo-inline-title">
                                <span>Robot Head</span>
                              </div>
                              {renderServoModeToggle("head")}
                            </div>
                            <div className="servo-inline-split servo-inline-split-two">
                              <div className="servo-inline-split-card">
                                <strong className="servo-inline-split-title">Pan 1</strong>
                                <div className="servo-inline-readout">
                                  <strong>{Number.isFinite(pan1State?.angle) ? `${pan1State.angle}°` : "-"}</strong>
                                </div>
                                <label className="b-axis-tune-field servo-dashboard-field servo-inline-field">
                                  <input
                                    type="number"
                                    min={pan1Servo.min}
                                    max={pan1Servo.max}
                                    step="1"
                                    placeholder="deg"
                                    value={servoAngleInputs[pan1Servo.key] ?? ""}
                                    onChange={(event) => updateServoAngleInput(pan1Servo.key, event.target.value)}
                                    disabled={bAxisCommandPending || servoControlModes.head !== "manual"}
                                  />
                                </label>
                                <button
                                  className="ghost-pill servo-dashboard-apply"
                                  type="button"
                                  onClick={() => applyServoAngleCommand(pan1Servo.key, pan1Angle)}
                                  disabled={bAxisCommandPending || servoControlModes.head !== "manual"}
                                >
                                  Apply
                                </button>
                              </div>
                              <div className="servo-inline-split-card">
                                <strong className="servo-inline-split-title">Pan 2</strong>
                                <div className="servo-inline-readout">
                                  <strong>{Number.isFinite(pan2State?.angle) ? `${pan2State.angle}°` : "-"}</strong>
                                </div>
                                <label className="b-axis-tune-field servo-dashboard-field servo-inline-field">
                                  <input
                                    type="number"
                                    min={pan2Servo.min}
                                    max={pan2Servo.max}
                                    step="1"
                                    placeholder="deg"
                                    value={servoAngleInputs[pan2Servo.key] ?? ""}
                                    onChange={(event) => updateServoAngleInput(pan2Servo.key, event.target.value)}
                                    disabled={bAxisCommandPending || servoControlModes.head !== "manual"}
                                  />
                                </label>
                                <button
                                  className="ghost-pill servo-dashboard-apply"
                                  type="button"
                                  onClick={() => applyServoAngleCommand(pan2Servo.key, pan2Angle)}
                                  disabled={bAxisCommandPending || servoControlModes.head !== "manual"}
                                >
                                  Apply
                                </button>
                              </div>
                            </div>
                          </div>
                        </article>
                      ) : null}

                      {otherServos.map((servo) => {
                  const currentServoState = servoStates[servo.key];
                  const currentAngle = currentServoState?.angle;
                  const currentPulseUs = currentServoState?.pulseUs;
                  const servoGlyph = renderServoGlyph(servo.key);
                  const parsedInputAngle = Number.parseInt(servoAngleInputs[servo.key] ?? "", 10);
                  const dialAngle = Number.isFinite(parsedInputAngle)
                    ? clampServoDialValue(parsedInputAngle, servo.min, servo.max)
                    : clampServoDialValue(Number.isFinite(currentAngle) ? currentAngle : 90, servo.min, servo.max);

                      if (servo.key === "lid") {
                        return (
                          <article key={servo.key} className="next-step-card servo-dashboard-card servo-dashboard-card-composite">
                            <div className="servo-inline-row servo-inline-row-single">
                              <div className="servo-inline-heading">
                                <div className="servo-dashboard-title servo-inline-title">
                                  <span>{servo.label}</span>
                                </div>
                                {renderServoModeToggle("lid")}
                              </div>
                              <div className="servo-inline-split servo-inline-split-two servo-inline-split-lid">
                                <div className="servo-inline-split-card servo-inline-split-card-lid">
                                  <strong className="servo-inline-split-title">{servo.label}</strong>
                                  <div className="servo-inline-readout">
                                    <strong>{Number.isFinite(currentAngle) ? `${currentAngle}°` : "-"}</strong>
                                  </div>
                                  <label className="b-axis-tune-field servo-dashboard-field servo-inline-field">
                                    <input
                                      type="number"
                                      min={servo.min}
                                      max={servo.max}
                                      step="1"
                                      placeholder="deg"
                                      value={servoAngleInputs[servo.key] ?? ""}
                                      onChange={(event) => updateServoAngleInput(servo.key, event.target.value)}
                                      disabled={bAxisCommandPending || servoControlModes.lid !== "manual"}
                                    />
                                  </label>
                                  <button
                                    className="ghost-pill servo-dashboard-apply"
                                    type="button"
                                    onClick={() => applyServoAngleCommand(servo.key, dialAngle)}
                                    disabled={bAxisCommandPending || servoControlModes.lid !== "manual"}
                                  >
                                    Apply
                                  </button>
                                </div>
                              </div>
                            </div>
                      </article>
                    );
                  }

                  return (
                    <article key={servo.key} className="next-step-card servo-dashboard-card">
                      <div className="servo-dashboard-head">
                        <div>
                          <div className="servo-dashboard-title">
                            {servoGlyph}
                            <span>{servo.label}</span>
                          </div>
                          {Number.isFinite(currentAngle) ? <strong>{`${currentAngle}°`}</strong> : null}
                        </div>
                        {Number.isFinite(currentPulseUs) ? <p>{`${currentPulseUs} us`}</p> : null}
                      </div>

                      <div className="servo-dashboard-body">
                        <ServoDial
                          min={servo.min}
                          max={servo.max}
                          value={dialAngle}
                          disabled={bAxisCommandPending}
                          onChange={(nextAngle) => updateServoAngleInput(servo.key, String(nextAngle))}
                        />

                        <button
                          className="ghost-pill servo-dashboard-apply"
                          type="button"
                          onClick={() => applyServoAngleCommand(servo.key, dialAngle)}
                          disabled={bAxisCommandPending}
                        >
                          Apply
                        </button>
                      </div>
                    </article>
                  );
                      })}

                      <article className="next-step-card servo-dashboard-card">
                        <div className="servo-dashboard-head byj-dashboard-head">
                          <div>
                            <span>Scraper</span>
                            <strong>{byj1State?.moving ? "Moving" : "Idle"}</strong>
                          </div>
                          {renderByjModeToggle("scraper")}
                        </div>

                        <div className="byj-status-grid">
                          <div className="next-step-card">
                            <span>Position</span>
                            <strong>{Math.round(byj1CurrentMm)} mm</strong>
                          </div>
                        </div>

                        <div className="servo-dashboard-body">
                          <label className="b-axis-tune-field servo-dashboard-field">
                            <span>Target mm</span>
                            <input
                              type="number"
                              min="0"
                              step="0.001"
                              value={byj1TargetMmInput}
                              onChange={(event) => setByj1TargetMmInput(event.target.value)}
                              disabled={bAxisCommandPending}
                            />
                          </label>

                          <button
                            className="ghost-pill servo-dashboard-apply"
                            type="button"
                            onClick={applyByj1GotoMm}
                            disabled={bAxisCommandPending}
                          >
                            Apply
                          </button>
                        </div>

                        <div className="servo-dashboard-presets">
                          <button className="ghost-pill" type="button" onClick={homeByj1} disabled={bAxisCommandPending}>
                            Home
                          </button>
                          <button className="ghost-pill" type="button" onClick={stopByj1} disabled={bAxisCommandPending}>
                            Stop
                          </button>
                          <button className="ghost-pill" type="button" onClick={parkByj1} disabled={bAxisCommandPending}>
                            Park
                          </button>
                        </div>
                      </article>

                      <article className="next-step-card servo-dashboard-card">
                        <div className="servo-dashboard-head byj-dashboard-head">
                          <div>
                            <span>Feeder</span>
                            <strong>{byj2State?.moving ? "Moving" : "Idle"}</strong>
                          </div>
                          {renderByjModeToggle("feeder")}
                        </div>

                        <div className="servo-dashboard-body">
                          <label className="b-axis-tune-field servo-dashboard-field">
                            <span>Step</span>
                            <input
                              type="number"
                              min="1"
                              step="1"
                              value={byj2StepInput}
                              onChange={(event) => setByj2StepInput(event.target.value)}
                              disabled={bAxisCommandPending}
                            />
                          </label>

                          <button
                            className="ghost-pill servo-dashboard-apply"
                            type="button"
                            onClick={byj2State?.moving ? pauseByj2 : runByj2Move}
                            disabled={bAxisCommandPending}
                          >
                            {byj2State?.moving ? "Pause" : "Run"}
                          </button>
                        </div>

                        <div className="servo-dashboard-presets">
                          <button className="ghost-pill" type="button" onClick={() => jogByj2("+")} disabled={bAxisCommandPending}>
                            Jog +
                          </button>
                          <button className="ghost-pill" type="button" onClick={() => jogByj2("-")} disabled={bAxisCommandPending}>
                            Jog -
                          </button>
                        </div>
                      </article>
                    </>
                  );
                })()}
              </div>
          </div>
          ) : null}

        </section>
      ) : null}

      {page.bridge?.bAxisTuning && showWorkspaceTab ? (
        <section
          ref={(element) => {
            sectionRefs.current.workspace = element;
          }}
          className="ab-workspace-shell"
        >
          <div className="ab-workspace-layout">
            <div className="ab-workspace-panel ab-workspace-side-panel ab-workspace-stats-panel">
              <div className="ab-workspace-stats">
              </div>
            </div>

            <div className="ab-workspace-panel ab-workspace-map-panel">
              <div className="ab-workspace-head">
                <div className="ab-workspace-head-status">
                  <div className="device-status-pill">{abMotionLabel}</div>
                  <button
                    type="button"
                    className={`device-status-pill device-status-pill-button device-status-pill-state-${axisAStatusKey}`}
                    onClick={homeAAxis}
                    disabled={abCommandLocked}
                  >
                    {axisAStatusLabel}
                  </button>
                  <button
                    type="button"
                    className={`device-status-pill device-status-pill-button device-status-pill-state-${axisBStatusKey}`}
                    onClick={homeBAxis}
                    disabled={abCommandLocked}
                  >
                    {axisBStatusLabel}
                  </button>
                </div>
                <strong>
                  A {effectiveTargetA.toFixed(2)} mm / B {effectiveTargetB.toFixed(2)} mm
                </strong>
              </div>
              <ABWorkspaceMap
                aTravelMm={aTravelMm}
                bTravelMm={bTravelMm}
                currentAMarker={currentAMarker}
                currentBMarker={currentBMarker}
                targetAMarker={targetAMarker}
                targetBMarker={targetBMarker}
                selectedAMarker={selectedAMarker}
                selectedBMarker={selectedBMarker}
                onSelect={selectABTarget}
                onApply={applyABTargetMm}
                selectedState={selectedTargetState}
              />
            </div>

            <div className="ab-workspace-panel ab-workspace-side-panel ab-workspace-controls-panel">
              <div className="b-axis-tune-grid">
                <div className="next-step-card b-axis-tune-field">
                  <span>A Target (mm)</span>
                  <input
                    type="number"
                    step="0.01"
                    min="0"
                    max={aTravelMm > 0 ? aTravelMm : undefined}
                    value={selectedATargetMmInput}
                    disabled={abCommandLocked}
                    onChange={(event) => {
                      const nextA = event.target.value;
                      setSelectedATargetMmInput(nextA);
                      if (nextA !== "" && selectedBTargetMmInput !== "") {
                        const parsedA = clampNumber(Number.parseFloat(nextA), 0, aTravelMm);
                        const parsedB = clampNumber(Number.parseFloat(selectedBTargetMmInput), 0, bTravelMm);
                        if (Number.isFinite(parsedA) && Number.isFinite(parsedB)) {
                          setSelectedABTarget({ aTargetMm: parsedA, bTargetMm: parsedB });
                        }
                      }
                    }}
                  />
                </div>
                <div className="next-step-card b-axis-tune-field">
                  <span>B Target (mm)</span>
                  <input
                    type="number"
                    step="0.01"
                    min="0"
                    max={bTravelMm > 0 ? bTravelMm : undefined}
                    value={selectedBTargetMmInput}
                    disabled={abCommandLocked}
                    onChange={(event) => {
                      const nextB = event.target.value;
                      setSelectedBTargetMmInput(nextB);
                      if (selectedATargetMmInput !== "" && nextB !== "") {
                        const parsedA = clampNumber(Number.parseFloat(selectedATargetMmInput), 0, aTravelMm);
                        const parsedB = clampNumber(Number.parseFloat(nextB), 0, bTravelMm);
                        if (Number.isFinite(parsedA) && Number.isFinite(parsedB)) {
                          setSelectedABTarget({ aTargetMm: parsedA, bTargetMm: parsedB });
                        }
                      }
                    }}
                  />
                </div>
              </div>
              <div className="featured-action-group b-axis-tune-actions">
                <button
                  className="ghost-pill"
                  type="button"
                  onClick={() => {
                    const aTargetMm = Number((aTravelMm / 2).toFixed(2));
                    const bTargetMm = Number((bTravelMm / 2).toFixed(2));
                    setSelectedABTarget({ aTargetMm, bTargetMm });
                    setSelectedATargetMmInput(String(aTargetMm));
                    setSelectedBTargetMmInput(String(bTargetMm));
                  }}
                  disabled={abCommandLocked}
                >
                  Select Center
                </button>
                <button className="ghost-pill" type="button" onClick={sendABNowFromInputs} disabled={abCommandLocked}>
                  Apply
                </button>
                <button
                  className="ghost-pill"
                  type="button"
                  onClick={() => {
                    setSelectedABTarget(null);
                    setSelectedATargetMmInput("");
                    setSelectedBTargetMmInput("");
                  }}
                  disabled={abCommandLocked}
                >
                  Clear
                </button>
                <button className="ghost-pill" type="button" onClick={homeAAxis} disabled={abCommandLocked}>
                  Home A
                </button>
                <button className="ghost-pill" type="button" onClick={homeBAxis} disabled={abCommandLocked}>
                  Home B
                </button>
                <button className="ghost-pill" type="button" onClick={scanAAxisTravel} disabled={abCommandLocked}>
                  Scan A
                </button>
                <button className="ghost-pill" type="button" onClick={scanBAxisTravel} disabled={abCommandLocked}>
                  Scan B
                </button>
                <button className="ghost-pill" type="button" onClick={parkABAxes} disabled={abCommandLocked}>
                  Park
                </button>
                <button className="ghost-pill" type="button" onClick={stopABMotion} disabled={bAxisCommandPending}>
                  Stop All
                </button>
              </div>
            </div>
          </div>
        </section>
      ) : null}

    </>
  );
}

export default ModulePage;
