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

Current pin map:

```text
Relay 1: GPIO5, active LOW
Relay 2: GPIO12, active LOW
Touch 1: GPIO4, active LOW, internal pullup
Touch 2: GPIO13, active LOW, internal pullup
Touch 3: GPIO14, active LOW, internal pullup
LED 1  : GPIO15
LED 2  : GPIO0
LED 3  : GPIO16
```

`GPIO0` and `GPIO15` are ESP8266 boot strap pins. Do not add external
circuits that force them to the wrong boot level. GPIO16 is used for LED 3
because GPIO2 blocked boot with the current LED wiring.
