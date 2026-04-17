# Bedroom 2 Node 02

ESP-12S firmware for a linked bedroom switch node.

Included:
- 1 touch
- 1 LED
- MQTT
- OTA
- remote relay control for `bedroom-2-node-01`

Pin map:
- touch: GPIO16
- led: GPIO14

Behavior:
- touch press sends `toggle_relay` to `bedroom-1-node-01`
- LED mirrors the relay state reported by `bedroom-1-node-01`
- while the touch is physically pressed, LED is forced full on

MQTT topics:
- `smarthome/bedroom-2-node-02/availability`
- `smarthome/bedroom-2-node-02/telemetry`
- `smarthome/bedroom-2-node-02/command`
- `smarthome/bedroom-2-node-02/state`
