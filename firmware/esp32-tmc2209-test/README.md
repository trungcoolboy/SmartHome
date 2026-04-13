# ESP32 TMC2209 UART Test

Project `PlatformIO` toi gian de test `TMC2209` bang `ESP32`.

## Wiring

- `GPIO17` -> `PDN_UART`
- `GPIO16` -> `PDN_UART` qua `1k`
- `GPIO32` -> `ENN`
- `GPIO12` -> `STEP`
- `GPIO13` -> `DIR`
- `ESP32 GND` -> `TMC2209 GND`
- `ESP32 3V3` -> `TMC2209 VIO`
- `nguon motor +` -> `VM`
- `nguon motor GND` -> `TMC2209 GND`

Mac dinh dia chi:

- `MS1 = GND`
- `MS2 = GND`
- `ADDR = 0`

## Expected serial output

Neu UART thong, monitor se thay:

- `IFCNT before=... after=...` co thay doi
- `IOIN=0x...` khac `0`

## Commands

- `s` -> in status (`IFCNT`, `IOIN`)
- `r` -> quay motor 400 step toi/lui

