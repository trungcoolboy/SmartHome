# STM32 #02 TMC2209 UART Test

Minimal firmware to isolate `TMC2209` UART on `STM32G431RB`.

Uses:
- `USART1` host log/command on `PC4/PC5`
- `USART2` driver UART on `PA2/PA3`
- `AB_EN` on `PB2` active-low

Boot behavior:
- enables axis A
- probes `addr 0`
- probes `addr 2`
- dumps raw UART RX bytes

Host commands over `USART1`:
- `status`
- `enable on`
- `enable off`
- `probe`
- `raw`
