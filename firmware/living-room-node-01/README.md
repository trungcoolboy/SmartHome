# Living Room Node 01

ESP-12S room node for:

- 1 relay
- 1 LED
- Wi-Fi + MQTT
- Arduino OTA

Default topics:

- `smarthome/living-room-node-01/availability`
- `smarthome/living-room-node-01/telemetry`
- `smarthome/living-room-node-01/command`
- `smarthome/living-room-node-01/state`

Supported MQTT commands:

- `{"action":"ping"}`
- `{"action":"set_relay","value":true}`
- `{"action":"toggle_relay"}`

Default pin map is in `include/node_config.h`.
