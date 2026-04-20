export const navItems = [
  { id: "overview", label: "Overview" },
  { id: "living-room", label: "Living Room" },
  { id: "bedroom-1", label: "Bedroom 1" },
  { id: "bedroom-2", label: "Bedroom 2" },
  { id: "bathroom-1", label: "Bathroom 1" },
  { id: "bathroom-2", label: "Bathroom 2" },
  { id: "aquarium", label: "Aquarium" },
  { id: "alerts", label: "Alerts" },
  { id: "settings", label: "Settings" },
];

export const overviewStats = [
  { label: "Devices Online", value: "18/21", tone: "good" },
  { label: "Open Alerts", value: "02", tone: "warn" },
  { label: "Aqua Water Temp", value: "27.4 C", tone: "neutral" },
  { label: "Lighting Scene", value: "Sunrise", tone: "accent" },
];

export const systemPulse = [
  { label: "MQTT Broker", value: "Online", detail: "1883 / retained state healthy", tone: "good" },
  { label: "Room Nodes", value: "5 zones", detail: "2 nodes need stronger RSSI", tone: "neutral" },
  { label: "Aqua Cluster", value: "3 boards", detail: "Serial bridges on 8081-8083", tone: "accent" },
];

export const quickActions = [
  "Feed Cycle",
  "Maintenance Mode",
  "All Lights Off",
  "Night Scene",
];

export const timelineEvents = [
  { time: "08:42", title: "Sunrise scene armed", detail: "Aqua Lighting moved to warm ramp over 25 min." },
  { time: "08:47", title: "Bathroom exhaust rule fired", detail: "Humidity crossed 70 percent threshold." },
  { time: "08:55", title: "Filter protection check", detail: "Aqua Core relay group validated with no fault." },
];

export const overviewModules = [
  {
    id: "living-room",
    title: "Living Room",
    description: "Khong gian sinh hoat chung voi den, rem va cam bien moi truong.",
    metrics: [
      { label: "Devices", value: "4 online" },
      { label: "Curtains", value: "Open 35%" },
      { label: "Air", value: "24.6 C / 58%" },
    ],
  },
  {
    id: "bedroom-1",
    title: "Bedroom 1",
    description: "Phong ngu chinh voi den, quat va sensor nhiet do do am.",
    metrics: [
      { label: "Devices", value: "3 online" },
      { label: "Fan", value: "Level 2" },
      { label: "Climate", value: "24.8 C / 61%" },
    ],
  },
  {
    id: "bedroom-2",
    title: "Bedroom 2",
    description: "Phong ngu phu uu tien thao tac nhanh va theo doi trang thai co ban.",
    metrics: [
      { label: "Devices", value: "3 online" },
      { label: "Desk Lamp", value: "Off" },
      { label: "Temperature", value: "25.1 C" },
    ],
  },
  {
    id: "bathroom-1",
    title: "Bathroom 1",
    description: "Phong tam chinh voi quat hut, den va tu dong xu ly do am.",
    metrics: [
      { label: "Humidity", value: "72%" },
      { label: "Exhaust", value: "Running" },
      { label: "Light", value: "On" },
    ],
  },
  {
    id: "bathroom-2",
    title: "Bathroom 2",
    description: "Phong tam phu voi canh bao ro ri va logic thong gio co ban.",
    metrics: [
      { label: "Humidity", value: "68%" },
      { label: "Leak Sensor", value: "Safe" },
      { label: "Exhaust", value: "Idle" },
    ],
  },
  {
    id: "aquarium",
    title: "Aquarium",
    description: "Core, motion va lighting duoc gom vao mot module.",
    metrics: [
      { label: "Bridges", value: "3 boards" },
      { label: "Life Support", value: "Online" },
      { label: "Motion / Light", value: "Ready" },
    ],
  },
];

export const pageContent = {
  overview: {
    eyebrow: "Overview",
    title: "One interface for rooms, water systems and motion control.",
    description:
      "The visual language is based on iOS/HomeKit cards, but the information density is tuned for pump, relay, sensor and actuator control.",
  },
  "living-room": {
    eyebrow: "Living Room",
    title: "Primary controls for the living room screen and daily media use.",
    description:
      "This view is centered on the TV bridge so power, input, volume and remote actions stay usable even when the room node layer is still being wired in.",
    featuredDevice: {
      eyebrow: "Connected Device",
      kind: "LG webOS TV",
      name: "Living Room TV",
      address: "192.168.1.52",
      apiPath: "/api/tv/living-room",
      connectivity: "Waiting for bridge status",
      note: "Status, input list and remote actions are routed through the local Smart Home API gateway.",
      nowPlaying: {
        title: "No active source yet",
        subtitle: "Foreground source from bridge",
        detail: "Bridge will update this card as soon as the TV reports its current app or input.",
      },
      facts: [
        { label: "Platform", value: "webOS" },
        { label: "IP", value: "192.168.1.52" },
        { label: "Transport", value: "LAN / pairing" },
      ],
      actions: ["Power", "Mute", "Source", "Settings"],
      transportActions: ["Home", "Back", "Up", "Left", "OK", "Right", "Down", "CH+", "CH-"],
      volume: {
        level: 14,
        mode: "Internal speakers",
      },
      apps: ["YouTube", "Netflix", "HDMI 1", "Live TV"],
    },
    roomNode: {
      title: "Living Room Node 02",
      apiPath: "/api/node/living-room-02",
      relays: [
        { key: "relay1", label: "Relay 1" },
        { key: "relay2", label: "Relay 2" },
      ],
      ledModes: [
        { key: "auto", label: "Auto" },
        { key: "on", label: "On" },
        { key: "off", label: "Off" },
        { key: "breathe", label: "Breathe" },
        { key: "blink_slow", label: "Blink Slow" },
        { key: "blink_fast", label: "Blink Fast" },
        { key: "double_blink", label: "Double Blink" },
        { key: "heartbeat", label: "Heartbeat" },
        { key: "pulse", label: "Pulse" },
        { key: "candle", label: "Candle" },
      ],
      touches: [
        { key: "relay1", label: "Touch 1" },
        { key: "relay2", label: "Touch 2" },
      ],
    },
    roomNodeSecondary: {
      title: "Living Room Node 01",
      apiPath: "/api/node/living-room-01",
      relays: [{ key: "relay", label: "Relay" }],
      touches: [{ key: "touch", label: "Touch" }],
      ledModes: [
        { key: "auto", label: "Auto" },
        { key: "on", label: "On" },
        { key: "off", label: "Off" },
        { key: "breathe", label: "Breathe" },
        { key: "blink_slow", label: "Blink Slow" },
        { key: "blink_fast", label: "Blink Fast" },
        { key: "double_blink", label: "Double Blink" },
        { key: "heartbeat", label: "Heartbeat" },
        { key: "pulse", label: "Pulse" },
        { key: "candle", label: "Candle" },
      ],
    },
    highlights: [],
  },
  "bedroom-1": {
    eyebrow: "Bedroom 1",
    title: "Primary bedroom controls for rest, lighting and climate.",
    description:
      "This room should surface sleep-oriented presets first, with fan and humidity data visible without opening device detail panels.",
    highlights: [
      {
        title: "Devices",
        items: ["Bedside lamp", "Ceiling light", "Wall fan", "Temperature and humidity sensor"],
      },
      {
        title: "Scenes",
        items: ["Sleep mode", "Reading mode", "Wake-up light", "Quiet cooling"],
      },
      {
        title: "Realtime Layer",
        items: ["Room temperature trend", "Humidity status", "Last manual override"],
      },
    ],
  },
  "bedroom-2": {
    eyebrow: "Bedroom 2",
    title: "Secondary bedroom controls with simple lighting and comfort states.",
    description:
      "This page should stay lightweight and fast, showing only the toggles and environment signals needed for daily use.",
    roomNode: {
      title: "Bedroom 2 Node 01",
      apiPath: "/api/node/bedroom-2-01",
      relays: [{ key: "relay", label: "Relay" }],
      touches: [{ key: "touch", label: "Touch" }],
      ledModes: [
        { key: "auto", label: "Auto" },
        { key: "on", label: "On" },
        { key: "off", label: "Off" },
        { key: "breathe", label: "Breathe" },
        { key: "blink_slow", label: "Blink Slow" },
        { key: "blink_fast", label: "Blink Fast" },
        { key: "double_blink", label: "Double Blink" },
        { key: "heartbeat", label: "Heartbeat" },
        { key: "pulse", label: "Pulse" },
        { key: "candle", label: "Candle" },
      ],
      ledModeScopedByRelay: false,
    },
    roomNodeSecondary: {
      title: "Bedroom 2 Node 02",
      apiPath: "/api/node/bedroom-2-02",
      touches: [{ key: "touch", label: "Touch" }],
      remoteRelayLabel: "Remote Relay",
      remoteRelayToggleAction: { action: "toggle_remote" },
      remoteRelaySyncAction: { action: "sync_remote" },
    },
    highlights: [
      {
        title: "Devices",
        items: ["Ceiling light", "Desk lamp", "Wall socket", "Temperature sensor"],
      },
      {
        title: "Scenes",
        items: ["Study mode", "Night mode", "Away mode", "All off"],
      },
      {
        title: "Realtime Layer",
        items: ["Occupancy state", "Node connection health", "Recent automation events"],
      },
    ],
  },
  "bathroom-1": {
    eyebrow: "Bathroom 1",
    title: "Bathroom controls focused on light, exhaust and moisture handling.",
    description:
      "This room should make humidity and exhaust logic obvious so fast manual overrides do not break basic ventilation automation.",
    highlights: [
      {
        title: "Devices",
        items: ["Mirror light", "Ceiling light", "Exhaust fan", "Humidity sensor"],
      },
      {
        title: "Automation",
        items: ["Auto-fan on high humidity", "Delayed fan off", "Night light trigger"],
      },
      {
        title: "Realtime Layer",
        items: ["Humidity trend", "Exhaust runtime", "Sensor heartbeat"],
      },
    ],
  },
  "bathroom-2": {
    eyebrow: "Bathroom 2",
    title: "Compact bathroom controls with moisture-safe automation defaults.",
    description:
      "This page should mirror the main bathroom interaction model while keeping device count and controls minimal.",
    highlights: [
      {
        title: "Devices",
        items: ["Ceiling light", "Exhaust fan", "Water leak sensor", "Humidity sensor"],
      },
      {
        title: "Automation",
        items: ["Leak alert routing", "Humidity exhaust rule", "Low-light night mode"],
      },
      {
        title: "Realtime Layer",
        items: ["Leak sensor state", "Humidity level", "Recent alert history"],
      },
    ],
  },
  aquarium: {
    hideHero: true,
    eyebrow: "Aquarium",
    title: "Unified aquarium controls for core life support, motion and lighting.",
    description:
      "This page groups the three STM32 aquarium subsystems into one module so daily operation stays in a single place.",
    deviceStrip: [
      { id: "control", label: "Control", icon: "control" },
      { id: "workspace", label: "AB Workspace", icon: "control" },
    ],
    bridge: {
      eyebrow: "Motion Bridge",
      kind: "STM32 #02",
      name: "Aquarium Motion",
      apiPath: "/api/stm32/02",
      note: "B-axis motion tuning is exposed here so travel length and near-endstop deceleration can be adjusted without reflashing.",
      bAxisTuning: {
        defaultATravelSteps: 49983,
        defaultTravelSteps: 5656,
        defaultDecelWindowSteps: 300,
        aMmPerStep: 0.0125,
        bMmPerStep: 0.05,
      },
    },
    pumpControl: {
      title: "Pump",
      apiPath: "/api/stm32/01",
      items: [
        { id: "in-pump", key: "in", label: "In Pump", mode: "auto", state: "off" },
        { id: "out-pump", key: "out", label: "Out Pump", mode: "auto", state: "off" },
        { id: "circulation-pump", key: "circulation", label: "Circulation Pump", mode: "auto", state: "on" },
        { id: "middle-pump", key: "middle", label: "Middle Pump", mode: "auto", state: "off" },
        { id: "filter-pump", key: "filter", label: "Filter Pump", mode: "auto", state: "on" },
        { id: "drain-pump", key: "drain", label: "Drain Pump", mode: "auto", state: "off" },
      ],
    },
    miscControl: {
      title: "M.I.S.C",
      apiPath: "/api/stm32/01",
      items: [
        { id: "oxygen", key: "oxygen", label: "Oxygen", mode: "auto", state: "off" },
        { id: "carbon-dioxide", key: "co2", label: "Carbon Dioxide", mode: "auto", state: "off" },
        { id: "tank-heater", key: "heater", label: "Tank Heater", mode: "auto", state: "off" },
        { id: "pretreat-heater", key: "pretreat_heater", label: "Pre-treat Heater", mode: "auto", state: "off" },
        { id: "water-inlet", key: "inlet", label: "Water Inlet", mode: "auto", state: "off" },
      ],
    },
    waterLevelSensors: {
      title: "Water Level Sensors",
      note: "Float switch active-low: water present pulls the input to GND.",
      items: [
        { id: "tank-low", label: "Tank Low", wet: false },
        { id: "tank-normal", label: "Tank Normal", wet: true },
        { id: "tank-high", label: "Tank High", wet: false },
        { id: "inlet-low", label: "Inlet Low", wet: false },
        { id: "inlet-high", label: "Inlet High", wet: false },
        { id: "pretreat-low", label: "Pre-treat Low", wet: false },
        { id: "pretreat-high", label: "Pre-treat High", wet: true },
        { id: "waste-low", label: "Waste Low", wet: false },
        { id: "waste-high", label: "Waste High", wet: false },
      ],
    },
    highlights: [
      {
        id: "control",
        title: "Control",
        items: ["Water level bands", "Water temperature", "Fill valve", "Main pump", "Filter pump", "Heater / O2 / CO2"],
      },
    ],
  },
  alerts: {
    eyebrow: "Alerts",
    title: "A unified queue for device faults, offline nodes and safety conditions.",
    description:
      "Alerts should stay short, actionable and linked back to the owning module so you can recover fast.",
    highlights: [
      {
        title: "Priority",
        items: ["Critical safety events", "Connectivity loss", "Rule violations"],
      },
      {
        title: "Ownership",
        items: ["Room nodes", "STM32 #1", "STM32 #2", "STM32 #3"],
      },
      {
        title: "Actions",
        items: ["Acknowledge", "Mute", "Open source module"],
      },
    ],
  },
  settings: {
    hideHero: true,
    eyebrow: "Settings",
    title: "Pinouts",
    description: "",
    deviceStrip: [{ id: "pinouts", label: "Pinouts", icon: "control" }],
    pinoutsCard: {
      title: "Pinouts",
      sections: [
        {
          title: "STM32 #01",
          items: [
            "3V3 rails: CN6-4, CN7-5 (VDD), CN7-16",
            "5V rails: CN6-5, CN7-18",
            "E5V: CN7-6",
            "5V_USB_CHGR: CN10-8",
            "GND: CN6-6, CN6-7, CN5-7, CN7-8, CN7-19, CN7-20, CN7-22, CN10-9, CN10-20",
            "AGND: CN10-32",
            "In Pump: PB0 / CN7-34",
            "Out Pump: PB1 / CN10-24",
            "Circulation Pump: PB2 / CN10-22",
            "Middle Pump: PB10 / CN10-25",
            "Filter Pump: PB11 / CN10-18",
            "Drain Pump: PB12 / CN10-16",
            "Oxygen: PB13 / CN10-30",
            "Carbon Dioxide: PB14 / CN10-28",
            "Tank Heater: PB15 / CN10-26",
            "Pre-treat Heater: PC6 / CN10-4",
            "Water Inlet: PC7 / CN5-2",
            "Tank Low: PA0 / CN7-28",
            "Tank Normal: PA1 / CN7-30",
            "Tank High: PA4 / CN7-32",
            "Inlet Low: PA6 / CN10-13",
            "Inlet High: PA7 / CN10-15",
            "Pre-treat Low: PC10 / CN7-1",
            "Pre-treat High: PC11 / CN7-2",
            "Waste Low: PB8 / CN10-3",
            "Waste High: PB9 / CN10-5",
          ],
        },
        {
          title: "STM32 #02",
          items: [
            "Host UART TX: PC4 / CN10-35",
            "Host UART RX: PC5 / CN10-37",
            "A STEP: PB0 / CN7-34",
            "A DIR: PB1 / CN10-24",
            "AB ENN: PB2 / CN10-22",
            "B STEP: PA8 / CN10-23",
            "B DIR: PC7 / CN5-2",
            "A Min Endstop: PA9 / CN5-1 (NC, INPUT_PULLUP)",
            "A Max Endstop: PC0 / CN7-38 (NC, INPUT_PULLUP)",
            "B Min Endstop: PB4 / CN10-27 (NC, INPUT_PULLUP)",
            "B Max Endstop: PB5 / CN10-29 (NC, INPUT_PULLUP)",
            "TMC2209 UART TX: PB10 / CN10-25",
            "TMC2209 UART RX: PB11 / CN10-18",
            "Fan1 Servo: PA6 / CN10-13",
            "Fan2 Servo: PA7 / CN10-15",
            "Pan Tilt 1: PB6 / CN10-17",
            "Pan Tilt 2: PA1 / CN7-30",
            "Lid Servo: PA4 / CN7-32",
            "Intel Fan 1 PWM: PC6 / CN10-4",
            "Intel Fan 1 TACH: PC9 / CN10-1",
            "Intel Fan 1 Power Relay: PA12 / CN10-12",
            "Intel Fan 2 PWM: PC8 / CN10-2",
            "Intel Fan 2 TACH: PB7 / CN7-21",
            "Intel Fan 2 Power Relay: PC1 / CN7-36",
            "OLED I2C SCL: PB8 / CN10-3",
            "OLED I2C SDA: PB9 / CN10-5",
            "28BYJ-48 #1 STEP: PB12 / CN10-16",
            "28BYJ-48 #1 DIR: PB13 / CN10-30",
            "28BYJ-48 #1 EN: PB14 / CN10-28 (TMC2209 EN, active-low, no UART)",
            "28BYJ-48 #1 Endstop: PB3 / CN10-31 (mechanical NC, pull-up)",
            "28BYJ-48 #2 STEP: PB15 / CN10-26",
            "28BYJ-48 #2 DIR: PC10 / CN7-1",
            "28BYJ-48 #2 EN: PC11 / CN7-2 (TMC2209 EN, active-low, no UART)",
            "Magnet PWM: PA11 / CN10-14",
            "TMC2209 A Address: MS1=GND, MS2=GND -> addr 0",
            "TMC2209 B Address: MS1=3.3V, MS2=3.3V -> addr 3",
          ],
        },
      ],
    },
    highlights: [],
  },
};

export const alertFeed = [
  { level: "Critical", source: "Aqua Core", text: "Low water interlock armed for filter protection." },
  { level: "Warning", source: "Garage Node", text: "Heartbeat delayed by 24 seconds." },
  { level: "Info", source: "Aqua Lighting", text: "Scene switched from Day to Sunset." },
];

export const pinnedDevices = [
  { name: "Living Room Lights", status: "On", meta: "82% brightness", tone: "accent" },
  { name: "Bedroom 1 Fan", status: "Level 2", meta: "Quiet overnight profile", tone: "good" },
  { name: "Aqua Heater", status: "Active", meta: "Target 27.5 C", tone: "warn" },
  { name: "Filter Pump", status: "Healthy", meta: "Runtime 14h 22m", tone: "good" },
];
