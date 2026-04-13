# STM32 #02

Firmware test UART cho `NUCLEO-G431RB` board `#02`.

## Ket noi

- `FT232RL TXD` -> `CN10 pin 37` (`PC5`, `USART1_RX`)
- `FT232RL RXD` -> `CN10 pin 35` (`PC4`, `USART1_TX`)
- `FT232RL GND` -> `GND`

Dat `FT232RL` o muc logic `3.3V`.

## Chuc nang firmware

- Khoi dong `USART1` tren `PC4/PC5`
- Baudrate `115200`, `8N1`
- Gui chuoi `alive N` moi giay
- Nhan 1 byte va echo lai theo dang `echo:X`
- Nhay LED `PA5` moi chu ky

## Build

```bash
cd /home/trungcoolboy/smart-home/firmware/stm32-02
make
```

## Flash

```bash
sudo openocd -f interface/stlink.cfg -f target/stm32g4x.cfg -c "program build/stm32_01_uart.elf verify reset exit"
```

## Doc serial

Qua ST-LINK VCP:

```bash
sudo stty -F /dev/ttyACM0 115200 raw -echo -echoe -echok
sudo cat /dev/ttyACM0
```

Qua FT232RL:

```bash
sudo stty -F /dev/ttyUSB2 115200 raw -echo -echoe -echok
sudo cat /dev/ttyUSB2
```
