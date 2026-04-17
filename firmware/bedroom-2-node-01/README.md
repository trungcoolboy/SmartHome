# Bedroom 2 Node 01

ESP-12S firmware scaffold for a single-channel bedroom node.

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
- `smarthome/bedroom-2-node-01/availability`
- `smarthome/bedroom-2-node-01/telemetry`
- `smarthome/bedroom-2-node-01/command`
- `smarthome/bedroom-2-node-01/state`

Common commands:

```json
{"action":"ping"}
{"action":"set_relay","value":true}
{"action":"toggle_relay"}
{"action":"set_led_mode","mode":"auto"}
```
