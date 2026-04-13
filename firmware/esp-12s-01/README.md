# ESP-12S #01

Firmware `ESP-12S` dau tien cho domain `Smart Home`.

## Muc tieu

- ket noi Wi-Fi
- ket noi MQTT broker tren Ubuntu
- publish telemetry dinh ky
- nhan command qua MQTT

## MQTT topics

- `smarthome/esp12s-01/availability`
- `smarthome/esp12s-01/telemetry`
- `smarthome/esp12s-01/command`
- `smarthome/esp12s-01/state`

## Cau hinh

Sua file [include/node_config.h](/home/trungcoolboy/smart-home/firmware/esp-12s-01/include/node_config.h):

- `kWifiSsid`
- `kWifiPassword`
- `kMqttHost`
- `kMqttPort`

Mac dinh `kMqttHost` dang tro toi `192.168.1.253`.

## Build

```bash
cd /home/trungcoolboy/smart-home/firmware/esp-12s-01
platformio run
```

## Flash

```bash
cd /home/trungcoolboy/smart-home/firmware/esp-12s-01
platformio run --target upload
```

## Test MQTT tren Ubuntu

Subscribe:

```bash
mosquitto_sub -h 127.0.0.1 -t 'smarthome/esp12s-01/#' -v
```

Gui lenh ping:

```bash
mosquitto_pub -h 127.0.0.1 -t 'smarthome/esp12s-01/command' -m '{"action":"ping"}'
```

Bat LED built-in:

```bash
mosquitto_pub -h 127.0.0.1 -t 'smarthome/esp12s-01/command' -m '{"action":"set_led","value":true}'
```

Tat LED built-in:

```bash
mosquitto_pub -h 127.0.0.1 -t 'smarthome/esp12s-01/command' -m '{"action":"set_led","value":false}'
```
