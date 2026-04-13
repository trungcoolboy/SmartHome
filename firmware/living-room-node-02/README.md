# Living Room Node 02

Firmware rieng cho `Living Room Node 02`.

Muc tieu:
- 2 relay
- 2 nut touch `TTP223/TTP232`
- 2 LED bao trang thai
- Wi-Fi + MQTT
- OTA update

## MQTT topics

- `smarthome/living-room-node-02/availability`
- `smarthome/living-room-node-02/telemetry`
- `smarthome/living-room-node-02/command`
- `smarthome/living-room-node-02/state`

## Command JSON

Ping:

```json
{"action":"ping"}
```

Bat/tat relay:

```json
{"action":"set_relay","channel":"relay1","value":true}
```

Toggle relay:

```json
{"action":"toggle_relay","channel":"relay2"}
```

## Pin config

Sua trong [node_config.h](/home/trungcoolboy/smart-home/firmware/living-room-node-02/include/node_config.h):

- `kRelay1Pin`
- `kRelay2Pin`
- `kTouch1Pin`
- `kTouch2Pin`
- `kLed1Pin`
- `kLed2Pin`

Neu relay/touch/LED cua may dao muc, sua:

- `kRelayActiveHigh`
- `kTouchActiveHigh`
- `kLedActiveHigh`

## OTA

Firmware nay da bat `ArduinoOTA`.

Mac dinh trong [node_config.h](/home/trungcoolboy/smart-home/firmware/living-room-node-02/include/node_config.h):

- `kOtaHostname = "living-room-node-02"`
- `kOtaPassword = ""`

Trong [platformio.ini](/home/trungcoolboy/smart-home/firmware/living-room-node-02/platformio.ini) da set san:

- `upload_protocol = espota`
- `upload_port = 192.168.1.180`

Sau khi may da co firmware OTA lan dau, cac lan sau chi can:

```bash
cd /home/trungcoolboy/smart-home/firmware/living-room-node-02
pio run -t upload
```
