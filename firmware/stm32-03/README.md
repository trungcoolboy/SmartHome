# STM32 #03

Firmware dieu khien truc `X/Y/Z`, LED PWM PT4115, LED fan PWM va NTC heatsink cho `NUCLEO-G431RB` board `#03`.

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

### LED PWM PT4115

PWM dang software `1 kHz`, duty `0..1000`.

| Channel | Pin |
| --- | --- |
| `led1` | `PA4` |
| `led2` | `PA6` |
| `led3` | `PA7` |
| `led4` | `PA12` |
| `led5` | `PA15` |
| `led6` | `PB3` |
| `led7` | `PB7` |
| `led8` | `PB8` |
| `led9` | `PB9` |
| `led10` | `PB10` |
| `led11` | `PB11` |

### LED Fan PWM

| Channel | Pin |
| --- | --- |
| `ledfan1` | `PB14` |
| `ledfan2` | `PB15` |

### LED Sink NTC 10K

- `led_sink`: `PA0` / `ADC1_IN1`
- Cong thuc NTC dung cung duong SH3 NTC 10K 3950 nhu STM32 #01.
- Mach mac dinh: `3.3V -> R 10K -> PA0 -> NTC -> GND`.

## Lenh UART

```text
status
pins
led status
led all 0
led 1 500
led led11 1000
fan status
fan 1 700
fan ledfan2 1000
temp
ntc
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
