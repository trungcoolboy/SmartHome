#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"

STM32_01_DEV="/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_BG01CM23-if00-port0"
STM32_02_DEV="/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_BG01OWDO-if00-port0"
STM32_03_DEV="/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_BG01OQ9Q-if00-port0"

exec \
  bash -lc "
    ${PYTHON_BIN} '${SCRIPT_DIR}/stm32_bridge.py' --board-id stm32-01 --device '${STM32_01_DEV}' --baudrate 115200 --port 8081 &
    ${PYTHON_BIN} '${SCRIPT_DIR}/stm32_bridge.py' --board-id stm32-02 --device '${STM32_02_DEV}' --baudrate 115200 --port 8082 &
    ${PYTHON_BIN} '${SCRIPT_DIR}/stm32_bridge.py' --board-id stm32-03 --device '${STM32_03_DEV}' --baudrate 115200 --port 8083 &
    wait
  "
