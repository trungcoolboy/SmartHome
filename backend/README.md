# Backend Bridge

Bridge Ubuntu cho `STM32 #01` va `STM32 #03`.

Co them service `LG webOS TV` cho `Living Room TV`.
Co them `Smart Home API gateway` de frontend chi can goi mot backend chung.

## Muc tieu

- doc serial tu `FT232RL` tren `/dev/ttyUSB0`
- giu state realtime cua `STM32 #01`
- mo HTTP API de dashboard hoac script khac doc/ghi du lieu

## Chay bridge

`STM32 #01`:

```bash
cd /home/trungcoolboy/smart-home/backend
python3 stm32_bridge.py --board-id stm32-01 --device /dev/serial/by-id/usb-FTDI_FT232R_USB_UART_BG01CM23-if00-port0 --baudrate 115200 --port 8081
```

`STM32 #03`:

```bash
cd /home/trungcoolboy/smart-home/backend
python3 stm32_bridge.py --board-id stm32-03 --device /dev/serial/by-id/usb-FTDI_FT232R_USB_UART_BG01OQ9Q-if00-port0 --baudrate 115200 --port 8083
```

`STM32 #02`:

```bash
cd /home/trungcoolboy/smart-home/backend
python3 stm32_bridge.py --board-id stm32-02 --device /dev/serial/by-id/usb-FTDI_FT232R_USB_UART_BG01OWDO-if00-port0 --baudrate 115200 --port 8082
```

Neu user chua co quyen serial, chay tam bang `sudo`.

## API

### Health

```bash
curl http://127.0.0.1:8081/health
```

### Status

```bash
curl http://127.0.0.1:8081/api/stm32/01/status
```

### Logs

```bash
curl "http://127.0.0.1:8081/api/stm32/01/logs?limit=20"
```

### Gui lenh xuong STM32

```bash
curl -X POST http://127.0.0.1:8081/api/stm32/01/send \
  -H "Content-Type: application/json" \
  -d '{"text":"ping"}'
```

### SSE events

```bash
curl -N http://127.0.0.1:8081/api/stm32/01/events
```

`STM32 #02` dung endpoint tuong tu tren `8082`:

```bash
curl http://127.0.0.1:8082/api/stm32/02/status
```

`STM32 #03` dung endpoint tuong tu tren `8083`:

```bash
curl http://127.0.0.1:8083/api/stm32/03/status
```

## LG webOS TV

Chay bridge:

```bash
cd /home/trungcoolboy/smart-home/backend
python3 webos_tv_bridge.py --host 192.168.1.52 --listen-port 8084
```

Status:

```bash
curl http://127.0.0.1:8084/api/tv/living-room/status
```

Refresh state that:

```bash
curl -X POST http://127.0.0.1:8084/api/tv/living-room/refresh \
  -H "Content-Type: application/json" \
  -d '{}'
```

Thu volume up:

```bash
curl -X POST http://127.0.0.1:8084/api/tv/living-room/command \
  -H "Content-Type: application/json" \
  -d '{"command":"volume_up"}'
```

## Smart Home API gateway

Chay gateway:

```bash
cd /home/trungcoolboy/smart-home/backend
python3 smart_home_api.py --port 8090
```

Health:

```bash
curl http://127.0.0.1:8090/health
```

TV qua gateway:

```bash
curl http://127.0.0.1:8090/api/tv/living-room/status
```

STM32 #01 qua gateway:

```bash
curl http://127.0.0.1:8090/api/stm32/01/status
```

## Du lieu hien tai tu firmware test

Firmware `STM32 #01` dang gui:

- `alive N`
- `echo:X`

Bridge se parse `alive N` thanh `aliveCounter` trong status.
