import React, { useEffect, useRef, useState } from "react";
import { getApiBaseUrl } from "../api";

function ModulePage({ page, alertFeed }) {
  const [deviceState, setDeviceState] = useState(null);
  const [deviceError, setDeviceError] = useState("");
  const [pendingPowerCommand, setPendingPowerCommand] = useState("");
  const [showVolumeHud, setShowVolumeHud] = useState(false);
  const [volumeHudValue, setVolumeHudValue] = useState(0);
  const [volumeHudDragging, setVolumeHudDragging] = useState(false);
  const [bridgeState, setBridgeState] = useState(null);
  const [bridgeLogs, setBridgeLogs] = useState([]);
  const [bridgeError, setBridgeError] = useState("");
  const [roomNodeState, setRoomNodeState] = useState(null);
  const [roomNodeError, setRoomNodeError] = useState("");
  const [roomNodeSecondaryState, setRoomNodeSecondaryState] = useState(null);
  const [roomNodeSecondaryError, setRoomNodeSecondaryError] = useState("");
  const [pumpStates, setPumpStates] = useState({});
  const [miscStates, setMiscStates] = useState({});
  const [sensorStates, setSensorStates] = useState({});
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

  function clampVolume(value) {
    return Math.max(0, Math.min(100, Math.round(value)));
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

    lines.forEach((line) => {
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
    if (!page.featuredDevice?.apiPath) {
      clearScheduledWork();
      setDeviceState(null);
      setDeviceError("");
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
    connectEvents();
    statusTimerRef.current = window.setInterval(() => {
      loadStatus({ preserveError: true });
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
            setBridgeError("");
          }
          if ((payload.type === "rx" || payload.type === "tx") && payload.payload) {
            if (payload.type === "rx") {
              applyBridgeLinesToControls([payload.payload]);
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
    }, 15000);

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
    ? `Bridge error: ${deviceError}`
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

      {page.featuredDevice ? (
        <section className="room-device-strip" aria-label="Room devices">
          <button
            className="room-device-icon active"
            type="button"
            aria-label={page.featuredDevice.name}
            title={page.featuredDevice.name}
            onClick={focusFeaturedDevice}
          >
            <svg viewBox="0 0 24 24" aria-hidden="true">
              <rect x="3" y="5" width="18" height="12" rx="2.5" />
              <path d="M9 20h6" />
              <path d="M12 17v3" />
            </svg>
          </button>
          {page.roomNode ? (
            <button
              className="room-device-icon active"
              type="button"
              aria-label={page.roomNode.title}
              title={page.roomNode.title}
              onClick={focusRoomNode}
            >
              {renderStripIcon("node")}
            </button>
          ) : null}
          {page.roomNodeSecondary ? (
            <button
              className="room-device-icon active"
              type="button"
              aria-label={page.roomNodeSecondary.title}
              title={page.roomNodeSecondary.title}
              onClick={focusRoomNodeSecondary}
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
              className="room-device-icon active"
              type="button"
              aria-label={item.label}
              title={item.label}
              onClick={() => focusPageSection(item.id)}
            >
              {renderStripIcon(item.icon)}
            </button>
          ))}
        </section>
      ) : null}

      {page.highlights?.length ? (
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
          <section ref={featuredDeviceCardRef} className="featured-device-card">
            <div className="featured-device-copy">
              <h3>{page.featuredDevice.name}</h3>
            </div>

            <div className="featured-device-actions">
              <div className="featured-actions-left">
                <div className="featured-actions-spacer" />
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
              <span className="eyebrow">Apps & Sources</span>
              <div className="chip-grid">
                {liveInputItems.map((app) => (
                  <button
                    key={`${app.label}-${app.appId || "mock"}`}
                    className="source-chip"
                    type="button"
                    onClick={() => {
                      if (app.appId) {
                        sendFeaturedCommand({ command: "set_input", appId: app.appId });
                      }
                    }}
                  >
                    {app.label}
                  </button>
                ))}
              </div>
            </article>
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
                    const relayLedMode = roomNodeState?.ledModes?.[relay.key] ?? "unknown";
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
                                    channel: relay.key,
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
                <div className="next-step-grid">
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
        </>
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

      {page.bridge ? (
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

            <div className="volume-card">
              <span>Telemetry</span>
              <strong>{bridgeState?.lastLine ?? "No line yet"}</strong>
              <p>
                {bridgeState?.aliveCounter != null
                  ? `Alive counter ${bridgeState.aliveCounter}`
                  : "No alive counter reported"}
              </p>
              <div className="next-step-grid">
                <div className="next-step-card">
                  <span>Bytes Rx</span>
                  <strong>{bridgeState?.bytesReceived ?? 0}</strong>
                </div>
                <div className="next-step-card">
                  <span>Bytes Tx</span>
                  <strong>{bridgeState?.bytesSent ?? 0}</strong>
                </div>
                <div className="next-step-card">
                  <span>Uptime</span>
                  <strong>
                    {bridgeState?.uptimeSeconds != null ? `${Math.round(bridgeState.uptimeSeconds)}s` : "n/a"}
                  </strong>
                </div>
              </div>
            </div>
          </div>
        </section>
      ) : null}

      {page.bridge ? (
        <section className="tv-control-grid">
          <article className="tv-control-card">
            <span className="eyebrow">Recent Bridge Logs</span>
            <div className="alert-stack">
              {bridgeLogs.length ? (
                bridgeLogs
                  .slice()
                  .reverse()
                  .map((item, index) => (
                    <div key={`${item.ts}-${item.direction}-${index}`} className="alert-row">
                      <strong>{item.direction.toUpperCase()}</strong>
                      <span>
                        {item.ts ? new Date(item.ts * 1000).toLocaleTimeString() : "No timestamp"}
                      </span>
                      <p>{item.payload}</p>
                    </div>
                  ))
              ) : (
                <div className="alert-row">
                  <strong>INFO</strong>
                  <span>Bridge</span>
                  <p>No logs received yet from this board.</p>
                </div>
              )}
            </div>
          </article>

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
