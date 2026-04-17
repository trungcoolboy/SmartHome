# Bathroom 1 Node 01

ESP-12S room node for:

- 1 relay
- 1 LED
- Wi-Fi + MQTT
- Arduino OTA

Default topics:

- `smarthome/bathroom-1-node-01/availability`
- `smarthome/bathroom-1-node-01/telemetry`
- `smarthome/bathroom-1-node-01/command`
- `smarthome/bathroom-1-node-01/state`

Supported MQTT commands:

- `{"action":"ping"}`
- `{"action":"set_relay","value":true}`
- `{"action":"toggle_relay"}`
- `{"action":"set_led_mode","mode":"auto"}`
- `{"action":"set_led_mode","mode":"on"}`
- `{"action":"set_led_mode","mode":"off"}`
- `{"action":"set_led_mode","mode":"breathe"}`
- `{"action":"set_led_mode","mode":"blink_slow"}`
- `{"action":"set_led_mode","mode":"blink_fast"}`
- `{"action":"set_led_mode","mode":"double_blink"}`
- `{"action":"set_led_mode","mode":"heartbeat"}`
- `{"action":"set_led_mode","mode":"pulse"}`
- `{"action":"set_led_mode","mode":"candle"}`

Default pin map is in `include/node_config.h`.
