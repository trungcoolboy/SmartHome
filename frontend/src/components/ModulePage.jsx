import React, { useEffect, useRef, useState } from "react";
import { getApiBaseUrl } from "../api";
import ABWorkspaceMap from "./ABWorkspaceMap";
import youtubeIcon from "../assets/tv-apps/youtube.svg";
import spotifyIcon from "../assets/tv-apps/spotify.svg";
import browserIcon from "../assets/tv-apps/browser.svg";
import fptPlayIcon from "../assets/tv-apps/fptplay.ico";
import vieonIcon from "../assets/tv-apps/vieon.ico";
import vtvGoIcon from "../assets/tv-apps/vtvgo.ico";

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

  const servoDefinitions = [
    { key: "fan1", label: "Fan 1", min: 0, max: 100 },
    { key: "fan2", label: "Fan 2", min: 70, max: 175 },
    { key: "pan1", label: "Pan 1", min: 0, max: 180 },
    { key: "pan2", label: "Pan 2", min: 50, max: 140 },
    { key: "lid", label: "Lid", min: 0, max: 180 },
  ];
  const byj1MmPerStep = 42 / 50000;

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
      /^axis\s+([ab])\s+enabled\s+(on|off)\s+moving\s+(on|off)\s+pos\s+(-?\d+)\s+target\s+(-?\d+)\s+vel\s+(-?\d+)\s+homed\s+(yes|no).*?\stravel\s+(\d+)/i,
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
        travel: Number.parseInt(axisMatch[8], 10),
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
          return;
        }

        if (parsed.kind === "ab") {
          next.ab = parsed;
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
          };
          return;
        }

        if (parsed.kind === "scan_ok") {
          const existingAxis = next[parsed.axis] ?? current[parsed.axis] ?? {};
          next[parsed.axis] = {
            ...existingAxis,
            enabled: true,
            moving: false,
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
          };
          return;
        }

        if (parsed.kind === "stop_ok") {
          const existingAxis = next[parsed.axis] ?? current[parsed.axis] ?? {};
          next[parsed.axis] = {
            ...existingAxis,
            moving: false,
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
          };
          return;
        }

        if (parsed.kind === "axis_motion") {
          const existingAxis = next[parsed.axis] ?? current[parsed.axis] ?? {};
          next[parsed.axis] = {
            ...existingAxis,
            moving: parsed.moving,
          };
          if (parsed.moving || parsed.homingState !== "idle") {
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
            };
            return;
          }
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
          };
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

    const nextPumps = {};
    const nextMisc = {};
    const nextSensors = {};
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

    const nextPumps = {};
    const nextMisc = {};
    const nextSensors = {};

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

    if (Object.keys(nextPumps).length) {
      setPumpStates((current) => ({ ...current, ...nextPumps }));
    }
    if (Object.keys(nextMisc).length) {
      setMiscStates((current) => ({ ...current, ...nextMisc }));
    }
    if (Object.keys(nextSensors).length) {
      setSensorStates((current) => ({ ...current, ...nextSensors }));
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
      setMotionState({ a: null, b: null, ab: null });
      return;
    }
    setBAxisTravelSteps(String(tuning.defaultTravelSteps ?? ""));
    setBAxisDecelWindowSteps(String(tuning.defaultDecelWindowSteps ?? ""));
    setBAxisGotoPosition("");
    setMotionState({
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

    async function loadBridgeLogs() {
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
      }
    }

    async function loadBridgeStatus(options = {}) {
      const preserveError = options.preserveError === true;
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
      }
    }

    function connectBridgeEvents() {
      if (cancelled) {
        return;
      }

      eventSource = new EventSource(`${baseUrl}/events`);
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
          setBridgeError("bridge realtime stream disconnected");
          eventSource.close();
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
      loadBridgeLogs();
    }, 1000);

    return () => {
      cancelled = true;
      if (eventSource) {
        eventSource.close();
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
  const liveLaunchPoints = Array.isArray(deviceState?.launchPoints) ? deviceState.launchPoints : [];
  const preferredLaunchApps = preferredLaunchAppIds
    .map((appId) => liveLaunchPoints.find((item) => item?.id === appId))
    .filter(Boolean);
  const featuredLaunchApps = preferredLaunchApps.length
    ? preferredLaunchApps
    : liveLaunchPoints.filter((item) => item?.id && item?.icon).slice(0, 6);
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
  const hasAquariumTabs = Boolean(page.bridge?.bAxisTuning && page.deviceStrip?.length > 1);
  const showControlTab = !hasAquariumTabs || activeModuleTab === "control";
  const showWorkspaceTab = hasAquariumTabs && activeModuleTab === "workspace";
  const showFeaturedDeviceTab = !page.featuredDevice || activeRoomDeviceTab === "featured";
  const showRoomNodeTab = !page.featuredDevice || activeRoomDeviceTab === "room-node";
  const showRoomNodeSecondaryTab = !page.featuredDevice || activeRoomDeviceTab === "room-node-secondary";
  const currentAMm = (motionState.ab?.posA ?? motionState.a?.pos ?? 0) * aMmPerStep;
  const currentBMm = (motionState.ab?.posB ?? motionState.b?.pos ?? 0) * bMmPerStep;
  const liveTargetAMm = (motionState.ab?.targetA ?? motionState.a?.target ?? 0) * aMmPerStep;
  const liveTargetBMm = (motionState.ab?.targetB ?? motionState.b?.target ?? 0) * bMmPerStep;
  const anyAxisMoving = Boolean(motionState.a?.moving || motionState.b?.moving || motionState.ab?.active);
  const effectiveTargetA = selectedABTarget?.aTargetMm ?? liveTargetAMm;
  const effectiveTargetB = selectedABTarget?.bTargetMm ?? liveTargetBMm;
  const currentAMarker = aTravelMm > 0 ? Math.max(0, Math.min(100, (currentAMm / aTravelMm) * 100)) : 0;
  const currentBMarker = bTravelMm > 0 ? Math.max(0, Math.min(100, (currentBMm / bTravelMm) * 100)) : 0;
  const targetAMarker = aTravelMm > 0 ? Math.max(0, Math.min(100, (effectiveTargetA / aTravelMm) * 100)) : 0;
  const targetBMarker = bTravelMm > 0 ? Math.max(0, Math.min(100, (effectiveTargetB / bTravelMm) * 100)) : 0;
  const selectedAMarker = aTravelMm > 0 && selectedABTarget ? Math.max(0, Math.min(100, (selectedABTarget.aTargetMm / aTravelMm) * 100)) : null;
  const selectedBMarker = bTravelMm > 0 && selectedABTarget ? Math.max(0, Math.min(100, (selectedABTarget.bTargetMm / bTravelMm) * 100)) : null;
  const parsedSelectedATargetMm = Number.parseFloat(selectedATargetMmInput);
  const parsedSelectedBTargetMm = Number.parseFloat(selectedBTargetMmInput);
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
      setBridgeState(statusPayload);
      setBridgeLogs((logsPayload.items ?? []).slice(-6));
      applyBridgeLinesToControls((logsPayload.items ?? []).map((item) => item.payload));
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

  async function homeBAxis() {
    await sendBridgeTextCommand("axis b home");
  }

  async function scanBAxisTravel() {
    await sendBridgeTextCommand("axis b scan");
  }

  async function homeAAxis() {
    await sendBridgeTextCommand("axis a home");
  }

  async function scanAAxisTravel() {
    await sendBridgeTextCommand("axis a scan");
  }

  async function sendABGoto(aTarget, bTarget) {
    await sendBridgeTextCommand(`ab goto ${aTarget} ${bTarget}`);
  }

  function parseABTargetFromInputs() {
    const aTargetMm = Number.parseFloat(selectedATargetMmInput);
    const bTargetMm = Number.parseFloat(selectedBTargetMmInput);

    if (!Number.isFinite(aTargetMm) || !Number.isFinite(bTargetMm)) {
      setBridgeError("enter valid A/B targets in mm");
      return null;
    }
    if (!motionState.a?.homed || !motionState.b?.homed) {
      setBridgeError("home A and B before sending an AB target");
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

  async function applySelectedABTarget() {
    const target = parseABTargetFromInputs();
    if (!target) {
      return;
    }
    await sendABGoto(target.aTargetSteps, target.bTargetSteps);
  }

  async function sendABNowFromInputs() {
    const target = parseABTargetFromInputs();
    if (!target) {
      return;
    }
    await sendABGoto(target.aTargetSteps, target.bTargetSteps);
  }

  async function stopABMotion() {
    await runBridgeCommandSequence(["ab stop", "axis a stop", "axis b stop"], 120);
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

  function renderStripIcon(icon) {
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
        await fetch(`${baseUrl}/send`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ text: "status" }),
        });
      } catch (error) {
        setBridgeError(error.message);
      }
    }, delayMs);
  }

  async function sendPumpCommand(item, commandText, onSuccess) {
    const baseUrl = getPumpBaseUrl();
    if (!baseUrl) {
      return;
    }

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
      setBridgeError("");
      requestPumpStatus(120);
    } catch (error) {
      setBridgeError(`${item.label}: ${error.message}`);
    }
  }

  function handlePumpModeChange(item, mode) {
    sendPumpCommand(item, `pump ${item.key} mode ${mode}`, () => {
      setPumpMode(item.id, mode);
    });
  }

  function handlePumpPowerChange(item, state) {
    sendPumpCommand(item, `pump ${item.key} ${state}`);
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
    sendPumpCommand(item, `misc ${item.key} mode ${mode}`, () => {
      setMiscMode(item.id, mode);
    });
  }

  function handleMiscPowerChange(item, state) {
    sendPumpCommand(item, `misc ${item.key} ${state}`);
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

      {!page.featuredDevice && page.deviceStrip?.length ? (
        <section className="room-device-strip" aria-label="Module sections">
          {page.deviceStrip.map((item) => (
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
                      <div className="sensor-grid">
                        {page.waterLevelSensors.items.map((item) => (
                          <div
                            key={item.id}
                            className={`sensor-pill ${(sensorStates[item.id]?.wet ?? false) ? "wet" : "dry"}`}
                          >
                            <div className="sensor-pill-head">
                              <span
                                className={`sensor-dot ${(sensorStates[item.id]?.wet ?? false) ? "wet" : "dry"}`}
                                aria-hidden="true"
                              />
                              <strong>{item.label}</strong>
                            </div>
                          </div>
                        ))}
                      </div>
                    </article>
                  ) : null}
                  <article className="pump-control-card">
                    <span className="eyebrow">{page.pumpControl.title}</span>
                    <div className="pump-list">
                      {page.pumpControl.items.map((item) => {
                        const pump = pumpStates[item.id] ?? { mode: item.mode ?? "auto", state: item.state ?? "off" };
                        const isManual = pump.mode === "manual";
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
                                  onClick={() => handlePumpModeChange(item, "auto")}
                                >
                                  Auto
                                </button>
                                <button
                                  className={`pump-toggle-button ${pump.mode === "manual" ? "active" : ""}`}
                                  type="button"
                                  onClick={() => handlePumpModeChange(item, "manual")}
                                >
                                  Manual
                                </button>
                              </div>
                              <div className="pump-power-group">
                                <button
                                  className={`pump-power-button on ${pump.state === "on" ? "active" : ""}`}
                                  type="button"
                                  disabled={!isManual}
                                  onClick={() => handlePumpPowerChange(item, "on")}
                                >
                                  On
                                </button>
                                <button
                                  className={`pump-power-button off ${pump.state === "off" ? "active" : ""}`}
                                  type="button"
                                  disabled={!isManual}
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
                                    onClick={() => handleMiscModeChange(item, "auto")}
                                  >
                                    Auto
                                  </button>
                                  <button
                                    className={`pump-toggle-button ${misc.mode === "manual" ? "active" : ""}`}
                                    type="button"
                                    onClick={() => handleMiscModeChange(item, "manual")}
                                  >
                                    Manual
                                  </button>
                                </div>
                                <div className="pump-power-group">
                                  <button
                                    className={`pump-power-button on ${misc.state === "on" ? "active" : ""}`}
                                    type="button"
                                    disabled={!isManual}
                                    onClick={() => handleMiscPowerChange(item, "on")}
                                  >
                                    On
                                  </button>
                                  <button
                                    className={`pump-power-button off ${misc.state === "off" ? "active" : ""}`}
                                    type="button"
                                    disabled={!isManual}
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
              <section ref={featuredDeviceCardRef} className="featured-device-card">
                <div className="featured-device-copy">
                  <h3>{page.featuredDevice.name}</h3>
                </div>

                <div className="featured-device-actions">
                  <div className="featured-actions-left">
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

              <section className="tv-control-grid">
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

      {page.bridge && showControlTab ? (
        <section className="featured-device-card">
          <div className="featured-device-copy">
            <span className="eyebrow">{page.bridge.eyebrow}</span>
            <h3>{page.bridge.name}</h3>
            <strong>{page.bridge.kind}</strong>
            <p>{page.bridge.note}</p>

            <div className="featured-now-playing">
              <span>Bridge Status</span>
              <strong>{liveBridgeConnectivity}</strong>
              <p>{bridgeState?.serialDevice ?? "Waiting for serial device path"}</p>
              <small>
                {bridgeError
                  ? `Bridge error: ${bridgeError}`
                  : bridgeState?.error
                    ? bridgeState.error
                    : bridgeState?.lastSeen
                      ? `Last line at ${new Date(bridgeState.lastSeen * 1000).toLocaleTimeString()}`
                      : "No serial payload received yet"}
              </small>
            </div>
          </div>

          <div className="featured-device-facts">
            <article className="featured-fact-card">
              <span>Board</span>
              <strong>{bridgeState?.boardId ?? "Unknown"}</strong>
            </article>
            <article className="featured-fact-card">
              <span>Baudrate</span>
              <strong>{bridgeState?.baudrate ?? "n/a"}</strong>
            </article>
            <article className="featured-fact-card">
              <span>Lines Rx</span>
              <strong>{bridgeState?.linesReceived ?? 0}</strong>
            </article>
          </div>

          <div className="featured-device-actions">
            <div className="device-status-pill">{liveBridgeConnectivity}</div>
            <div className="featured-action-group">
              <button className="ghost-pill" type="button" onClick={refreshBridgeStatus}>
                Refresh Bridge
              </button>
            </div>

            {page.bridge?.bAxisTuning ? (
              <div className="volume-card b-axis-tune-card">
                <span>B Axis Tune</span>
                <strong>STM32 #02 Motion</strong>
                <p>Home, scan travel, set travel length, set decel window, and move to a target position.</p>
                <div className="b-axis-tune-grid">
                  <div className="next-step-card b-axis-tune-field">
                    <span>Travel Steps</span>
                    <input
                      type="number"
                      value={bAxisTravelSteps}
                      onChange={(event) => setBAxisTravelSteps(event.target.value)}
                      disabled={bAxisCommandPending}
                    />
                    <button className="ghost-pill" type="button" onClick={applyBAxisTravel} disabled={bAxisCommandPending}>
                      Apply Travel
                    </button>
                  </div>
                  <div className="next-step-card b-axis-tune-field">
                    <span>Decel Window</span>
                    <input
                      type="number"
                      value={bAxisDecelWindowSteps}
                      onChange={(event) => setBAxisDecelWindowSteps(event.target.value)}
                      disabled={bAxisCommandPending}
                    />
                    <button className="ghost-pill" type="button" onClick={applyBAxisDecelWindow} disabled={bAxisCommandPending}>
                      Apply Decel
                    </button>
                  </div>
                  <div className="next-step-card b-axis-tune-field">
                    <span>Goto Position</span>
                    <input
                      type="number"
                      value={bAxisGotoPosition}
                      onChange={(event) => setBAxisGotoPosition(event.target.value)}
                      disabled={bAxisCommandPending}
                    />
                    <button className="ghost-pill" type="button" onClick={applyBAxisGoto} disabled={bAxisCommandPending}>
                      Go To
                    </button>
                  </div>
                </div>
                <div className="featured-action-group b-axis-tune-actions">
                  <button className="ghost-pill" type="button" onClick={homeBAxis} disabled={bAxisCommandPending}>
                    Home B
                  </button>
                  <button className="ghost-pill" type="button" onClick={scanBAxisTravel} disabled={bAxisCommandPending}>
                    Scan Travel
                  </button>
                  <button className="ghost-pill" type="button" onClick={() => sendBridgeTextCommand("status")} disabled={bAxisCommandPending}>
                    Refresh Motion
                  </button>
                </div>
              </div>
            ) : null}

            <div className="volume-card servo-control-card">
              <span>Servo Control</span>
              <strong>STM32 #02 Servo</strong>
              <div className="featured-action-group b-axis-tune-actions">
                <button className="ghost-pill" type="button" onClick={refreshServoStatus} disabled={bAxisCommandPending}>
                  Refresh All
                </button>
              </div>
              <div className="servo-dashboard-grid">
                {servoDefinitions.map((servo) => {
                  const currentServoState = servoStates[servo.key];
                  const currentAngle = currentServoState?.angle;
                  const currentPulseUs = currentServoState?.pulseUs;
                  const presets = [servo.min, 90, servo.max].filter(
                    (angle, index, values) => angle >= servo.min && angle <= servo.max && values.indexOf(angle) === index,
                  );

                  return (
                    <article key={servo.key} className="next-step-card servo-dashboard-card">
                      <div className="servo-dashboard-head">
                        <div>
                          <span>{servo.label}</span>
                          <strong>{Number.isFinite(currentAngle) ? `${currentAngle}°` : "Unknown"}</strong>
                        </div>
                        <p>{Number.isFinite(currentPulseUs) ? `${currentPulseUs} us` : "No status"}</p>
                      </div>

                      <div className="servo-dashboard-body">
                        <label className="b-axis-tune-field servo-dashboard-field">
                          <span>Angle</span>
                          <input
                            type="number"
                            min={servo.min}
                            max={servo.max}
                            step="1"
                            value={servoAngleInputs[servo.key] ?? ""}
                            onChange={(event) => updateServoAngleInput(servo.key, event.target.value)}
                            disabled={bAxisCommandPending}
                          />
                        </label>

                        <button
                          className="ghost-pill servo-dashboard-apply"
                          type="button"
                          onClick={() => applyServoAngleCommand(servo.key, servoAngleInputs[servo.key])}
                          disabled={bAxisCommandPending}
                        >
                          Apply
                        </button>
                      </div>

                      <div className="servo-dashboard-presets">
                        {presets.map((angle) => (
                          <button
                            key={`${servo.key}-${angle}`}
                            className="ghost-pill"
                            type="button"
                            onClick={() => {
                              updateServoAngleInput(servo.key, String(angle));
                              applyServoAngleCommand(servo.key, angle);
                            }}
                            disabled={bAxisCommandPending}
                          >
                            {angle}°
                          </button>
                        ))}
                      </div>
                    </article>
                  );
                })}
              </div>
            </div>

            <div className="volume-card servo-control-card">
              <span>BYJ1 Control</span>
              <strong>Home and absolute move in mm</strong>
              <div className="servo-dashboard-grid">
                <article className="next-step-card servo-dashboard-card">
                  <div className="servo-dashboard-head">
                    <div>
                      <span>BYJ1 State</span>
                      <strong>{byj1State?.moving ? "Moving" : "Idle"}</strong>
                    </div>
                    <p>{byj1State?.endstop === "trig" ? "Endstop trig" : "Endstop clear"}</p>
                  </div>

                  <div className="byj-status-grid">
                    <div className="next-step-card">
                      <span>Position</span>
                      <strong>{byj1CurrentMm.toFixed(3)} mm</strong>
                    </div>
                    <div className="next-step-card">
                      <span>Target</span>
                      <strong>{byj1TargetMm.toFixed(3)} mm</strong>
                    </div>
                    <div className="next-step-card">
                      <span>Steps</span>
                      <strong>{byj1State?.pos ?? 0}</strong>
                    </div>
                    <div className="next-step-card">
                      <span>Scale</span>
                      <strong>42 mm / 50000 step</strong>
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
                      Apply mm
                    </button>
                  </div>

                  <div className="servo-dashboard-presets">
                    <button className="ghost-pill" type="button" onClick={homeByj1} disabled={bAxisCommandPending}>
                      Home BYJ1
                    </button>
                    <button className="ghost-pill" type="button" onClick={refreshByjStatus} disabled={bAxisCommandPending}>
                      Refresh BYJ1
                    </button>
                  </div>
                </article>

                <article className="next-step-card servo-dashboard-card">
                  <div className="servo-dashboard-head">
                    <div>
                      <span>BYJ2 State</span>
                      <strong>{byj2State?.moving ? "Moving" : "Idle"}</strong>
                    </div>
                    <p>{byj2State?.enabled ? "Enabled" : "Disabled"}</p>
                  </div>

                  <div className="byj-status-grid">
                    <div className="next-step-card">
                      <span>Position</span>
                      <strong>{byj2State?.pos ?? 0} step</strong>
                    </div>
                    <div className="next-step-card">
                      <span>Target</span>
                      <strong>{byj2State?.target ?? 0} step</strong>
                    </div>
                    <div className="next-step-card">
                      <span>Velocity</span>
                      <strong>{byj2State?.vel ?? 0}</strong>
                    </div>
                    <div className="next-step-card">
                      <span>Direction</span>
                      <strong>{byj2Direction}</strong>
                    </div>
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

                    <div className="servo-dashboard-presets">
                      <button
                        className={`ghost-pill${byj2Direction === "+" ? " is-active" : ""}`}
                        type="button"
                        onClick={() => setByj2Direction("+")}
                        disabled={bAxisCommandPending}
                      >
                        Dir +
                      </button>
                      <button
                        className={`ghost-pill${byj2Direction === "-" ? " is-active" : ""}`}
                        type="button"
                        onClick={() => setByj2Direction("-")}
                        disabled={bAxisCommandPending}
                      >
                        Dir -
                      </button>
                    </div>

                    <button
                      className="ghost-pill servo-dashboard-apply"
                      type="button"
                      onClick={runByj2Move}
                      disabled={bAxisCommandPending}
                    >
                      Run
                    </button>
                  </div>

                  <div className="servo-dashboard-presets">
                    <button className="ghost-pill" type="button" onClick={pauseByj2} disabled={bAxisCommandPending}>
                      Pause
                    </button>
                    <button className="ghost-pill" type="button" onClick={() => jogByj2("+")} disabled={bAxisCommandPending}>
                      Jog +
                    </button>
                    <button className="ghost-pill" type="button" onClick={() => jogByj2("-")} disabled={bAxisCommandPending}>
                      Jog -
                    </button>
                    <button className="ghost-pill" type="button" onClick={refreshByjStatus} disabled={bAxisCommandPending}>
                      Refresh BYJ2
                    </button>
                  </div>
                </article>
              </div>
            </div>

          </div>
        </section>
      ) : null}

      {page.bridge?.bAxisTuning && showWorkspaceTab ? (
        <section
          ref={(element) => {
            sectionRefs.current.workspace = element;
          }}
          className="featured-device-card ab-workspace-shell"
        >
          <div className="ab-workspace-layout">
            <div className="ab-workspace-panel ab-workspace-side-panel ab-workspace-stats-panel">
              <div className="ab-workspace-stats">
                <div className="next-step-card">
                  <span>Selected A</span>
                  <strong>{selectedABTarget?.aTargetMm ?? "-"} mm</strong>
                </div>
                <div className="next-step-card">
                  <span>Selected B</span>
                  <strong>{selectedABTarget?.bTargetMm ?? "-"} mm</strong>
                </div>
                <div className="next-step-card">
                  <span>Axis A</span>
                  <strong>{motionState.a?.homed ? "Homed" : "Not Homed"}</strong>
                </div>
                <div className="next-step-card">
                  <span>Axis B</span>
                  <strong>{motionState.b?.homed ? "Homed" : "Not Homed"}</strong>
                </div>
                <div className="next-step-card">
                  <span>AB Motion</span>
                  <strong>{anyAxisMoving ? "Moving" : "Idle"}</strong>
                </div>
                <div className="next-step-card">
                  <span>Travel</span>
                  <strong>{aTravelMm.toFixed(2)} / {bTravelMm.toFixed(2)} mm</strong>
                </div>
              </div>
            </div>

            <div className="ab-workspace-panel ab-workspace-map-panel">
              <div className="ab-workspace-head">
                <div className="device-status-pill">{anyAxisMoving ? "Moving" : "Idle"}</div>
                <strong>
                  A {currentAMm.toFixed(2)} mm / B {currentBMm.toFixed(2)} mm
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
                  disabled={bAxisCommandPending}
                >
                  Select Center
                </button>
                <button className="ghost-pill" type="button" onClick={sendABNowFromInputs} disabled={bAxisCommandPending}>
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
                  disabled={bAxisCommandPending}
                >
                  Clear
                </button>
                <button className="ghost-pill" type="button" onClick={homeAAxis} disabled={bAxisCommandPending}>
                  Home A
                </button>
                <button className="ghost-pill" type="button" onClick={homeBAxis} disabled={bAxisCommandPending}>
                  Home B
                </button>
                <button className="ghost-pill" type="button" onClick={scanAAxisTravel} disabled={bAxisCommandPending}>
                  Scan A
                </button>
                <button className="ghost-pill" type="button" onClick={scanBAxisTravel} disabled={bAxisCommandPending}>
                  Scan B
                </button>
                <button className="ghost-pill" type="button" onClick={stopABMotion} disabled={bAxisCommandPending}>
                  Stop All
                </button>
              </div>
            </div>
          </div>
        </section>
      ) : null}

      {page.bridge ? (
        <section className="tv-control-grid">
          <article className="tv-control-card">
            <span className="eyebrow">Gateway Route</span>
            <div className="chip-grid">
              <button className="source-chip" type="button">
                {page.bridge.apiPath}/status
              </button>
              <button className="source-chip" type="button">
                {page.bridge.apiPath}/logs
              </button>
              <button className="source-chip" type="button">
                {page.bridge.apiPath}/events
              </button>
            </div>
          </article>
        </section>
      ) : null}
    </>
  );
}

export default ModulePage;
