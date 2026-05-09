#!/usr/bin/env bash
set -Eeuo pipefail

REPO_DIR="/home/trungcoolboy/SmartHome"

install -m 0755 "${REPO_DIR}/scripts/smarthome-usb-backup.sh" /usr/local/sbin/smarthome-usb-backup.sh
install -m 0644 "${REPO_DIR}/backend/systemd/smarthome-usb-backup.service" /etc/systemd/system/smarthome-usb-backup.service
install -m 0644 "${REPO_DIR}/backend/systemd/smarthome-usb-backup.timer" /etc/systemd/system/smarthome-usb-backup.timer

systemctl daemon-reload
systemctl enable --now smarthome-usb-backup.timer
systemctl list-timers --all --no-pager smarthome-usb-backup.timer
