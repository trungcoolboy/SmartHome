#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

sudo cp "${SCRIPT_DIR}/stm32-01-bridge.service" /etc/systemd/system/
sudo cp "${SCRIPT_DIR}/stm32-02-bridge.service" /etc/systemd/system/
sudo cp "${SCRIPT_DIR}/stm32-03-bridge.service" /etc/systemd/system/
sudo cp "${SCRIPT_DIR}/smart-home-telegram.service" /etc/systemd/system/
if [[ -f "${SCRIPT_DIR}/smart-home-telegram.env" ]]; then
  sudo install -m 600 -o root -g root "${SCRIPT_DIR}/smart-home-telegram.env" /etc/smart-home-telegram.env
fi

sudo systemctl daemon-reload
sudo systemctl enable --now stm32-01-bridge.service
sudo systemctl enable --now stm32-02-bridge.service
sudo systemctl enable --now stm32-03-bridge.service
if [[ -f /etc/smart-home-telegram.env ]]; then
  sudo systemctl enable --now smart-home-telegram.service
else
  echo "Skip smart-home-telegram.service: /etc/smart-home-telegram.env chua ton tai"
fi
