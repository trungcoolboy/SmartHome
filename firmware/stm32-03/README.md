# STM32 #03

Firmware dieu khien truc `X/Y/Z` cho `NUCLEO-G431RB` board `#03`.

## UART

- `FT232RL TXD` -> `CN10 pin 37` (`PC5`, `USART1_RX`)
- `FT232RL RXD` -> `CN10 pin 35` (`PC4`, `USART1_TX`)
- `FT232RL GND` -> `GND`
- Logic `3.3V`, baudrate `115200 8N1`

## Pinout

Endstop dang active-high: khi cham endstop thi input len `1/trig`; binh thuong can bi keo `GND/clear`.

| Axis | STEP | DIR | EN active-low | MIN | MAX |
| --- | --- | --- | --- | --- | --- |
| X | `PB0` / `CN7-34` | `PB1` / `CN10-24` | `PB2` / `CN10-22` | `PA9` / `CN5-1` | `PC0` / `CN7-38` |
| Y | `PA8` / `CN10-23` | `PC7` / `CN5-2` | `PB12` | `PB4` / `CN10-27` | `PB5` / `CN10-29` |
| Z | `PB6` / `CN10-17` | `PA1` / `CN7-30` | `PB13` | `PA10` | `PA11` / `CN10-14` |

## Lenh UART

```text
status
pins
axis x status
axis x enable on
axis x enable off
axis x home
axis x zero
axis x goto 10000
axis x move -500
axis x jog +
axis x jog -
axis x stop
axis x speed 160
axis x travel 100000
xyz status
xyz home
xyz goto 1000 2000 3000
xyz stop
```

Thay `x` bang `y` hoac `z` cho tung truc rieng.

## Build

```bash
cd /home/trungcoolboy/SmartHome/firmware/stm32-03
make
```

## Flash

Chi flash khi ST-LINK dang cam dung vao board `#03`.

```bash
cd /home/trungcoolboy/SmartHome/firmware/stm32-03
sudo openocd -f interface/stlink.cfg -f target/stm32g4x.cfg -c "program build/stm32_01_uart.elf verify reset exit"
```
