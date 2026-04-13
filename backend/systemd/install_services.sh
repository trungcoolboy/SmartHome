#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

sudo cp "${SCRIPT_DIR}/stm32-01-bridge.service" /etc/systemd/system/
sudo cp "${SCRIPT_DIR}/stm32-02-bridge.service" /etc/systemd/system/
sudo cp "${SCRIPT_DIR}/stm32-03-bridge.service" /etc/systemd/system/

sudo systemctl daemon-reload
sudo systemctl enable --now stm32-01-bridge.service
sudo systemctl enable --now stm32-02-bridge.service
sudo systemctl enable --now stm32-03-bridge.service
