# Bathroom 1 Node 02

ESP-12S firmware scaffold for a single-channel bathroom node.

Included:
- 1 relay
- 1 touch
- 1 LED
- MQTT
- OTA
- local-first touch handling
- LED modes

Current default pin map:
- relay: GPIO5
- led: GPIO4
- touch: GPIO16

MQTT topics:
- `smarthome/bathroom-1-node-02/availability`
- `smarthome/bathroom-1-node-02/telemetry`
- `smarthome/bathroom-1-node-02/command`
- `smarthome/bathroom-1-node-02/state`

Common commands:

```json
{"action":"ping"}
{"action":"set_relay","value":true}
{"action":"toggle_relay"}
{"action":"set_led_mode","mode":"auto"}
```
