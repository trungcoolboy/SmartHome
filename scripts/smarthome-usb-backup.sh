#!/usr/bin/env bash
set -Eeuo pipefail

USB_UUID="d53426ae-0ce8-481f-aa32-3d430e6b2e93"
MOUNT_POINT="/mnt/smarthome-backup"
BACKUP_ROOT="${MOUNT_POINT}/smarthome-backups"
HOST_NAME="$(hostname -s)"
KEEP_BACKUPS=14

REPO_DIR="/home/trungcoolboy/SmartHome"
STATE_DIR="${REPO_DIR}/backend/state"
UPLOAD_DIR="/home/trungcoolboy/smart-home/uploads/dashboard"

log() {
  printf '%s %s\n' "$(date -Is)" "$*"
}

copy_path_if_exists() {
  local src="$1"
  local dest_root="$2"
  if [[ ! -e "$src" ]]; then
    return 0
  fi
  if [[ -d "$src" ]]; then
    mkdir -p "${dest_root}${src}"
    rsync -a --numeric-ids "${src}/" "${dest_root}${src}/"
  else
    mkdir -p "${dest_root}$(dirname "$src")"
    rsync -a --numeric-ids "$src" "${dest_root}${src}"
  fi
}

if [[ "${EUID}" -ne 0 ]]; then
  echo "Run as root: sudo $0" >&2
  exit 1
fi

exec 9>/run/smarthome-usb-backup.lock
if ! flock -n 9; then
  log "backup already running"
  exit 0
fi

mkdir -p "$MOUNT_POINT"
if ! findmnt -rn "$MOUNT_POINT" >/dev/null; then
  mount "UUID=${USB_UUID}" "$MOUNT_POINT"
fi

if ! findmnt -rn "$MOUNT_POINT" >/dev/null; then
  log "usb backup mount failed"
  exit 1
fi

if ! mountpoint -q "$MOUNT_POINT"; then
  log "${MOUNT_POINT} is not a mountpoint"
  exit 1
fi

STAMP="$(date -u +%Y%m%d-%H%M%S)"
WORK_DIR="$(mktemp -d /tmp/smarthome-backup.XXXXXX)"
STAGING="${WORK_DIR}/smarthome-restore"
DEST_DIR="${BACKUP_ROOT}/${HOST_NAME}"
ARCHIVE="${DEST_DIR}/smarthome-config-${STAMP}.tar.gz"

cleanup() {
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT

mkdir -p "$STAGING" "$DEST_DIR"

log "backup staging started: ${STAGING}"

mkdir -p "${STAGING}${STATE_DIR}"
if [[ -d "$STATE_DIR" ]]; then
  rsync -a --numeric-ids \
    --exclude='*.sqlite3' \
    --exclude='*.sqlite3-wal' \
    --exclude='*.sqlite3-shm' \
    "${STATE_DIR}/" "${STAGING}${STATE_DIR}/"

  python3 - "$STATE_DIR" "${STAGING}${STATE_DIR}" <<'PY'
import sqlite3
import sys
from pathlib import Path

src_dir = Path(sys.argv[1])
dst_dir = Path(sys.argv[2])
dst_dir.mkdir(parents=True, exist_ok=True)

for src in src_dir.glob("*.sqlite3"):
    dst = dst_dir / src.name
    src_conn = sqlite3.connect(f"file:{src}?mode=ro", uri=True)
    try:
        dst_conn = sqlite3.connect(dst)
        try:
            src_conn.backup(dst_conn)
        finally:
            dst_conn.close()
    finally:
        src_conn.close()
PY
fi

copy_path_if_exists "$UPLOAD_DIR" "$STAGING"
copy_path_if_exists "/home/trungcoolboy/.ssh" "$STAGING"
copy_path_if_exists "/home/trungcoolboy/.gitconfig" "$STAGING"
copy_path_if_exists "/home/trungcoolboy/auto-push-smarthome.sh" "$STAGING"
copy_path_if_exists "/etc/smart-home-telegram.env" "$STAGING"
copy_path_if_exists "/etc/mosquitto/mosquitto.conf" "$STAGING"
copy_path_if_exists "/etc/mosquitto/conf.d" "$STAGING"
copy_path_if_exists "/etc/netplan" "$STAGING"
copy_path_if_exists "/etc/NetworkManager/system-connections" "$STAGING"

mkdir -p "${STAGING}/etc/systemd/system"
find /etc/systemd/system -maxdepth 1 -type f \( -name 'smart-home*.service' -o -name 'smart-home*.timer' -o -name 'smarthome*.service' -o -name 'smarthome*.timer' \) \
  -exec rsync -a --numeric-ids {} "${STAGING}/etc/systemd/system/" \;

mkdir -p "${STAGING}/system-info"
{
  echo "timestamp_utc=${STAMP}"
  echo "host=${HOST_NAME}"
  echo "repo=${REPO_DIR}"
  echo "usb_uuid=${USB_UUID}"
} >"${STAGING}/system-info/manifest.txt"

dpkg-query -W >"${STAGING}/system-info/packages.tsv" 2>/dev/null || true
systemctl list-unit-files --no-pager >"${STAGING}/system-info/systemd-unit-files.txt" 2>/dev/null || true
systemctl --failed --no-pager >"${STAGING}/system-info/systemd-failed.txt" 2>/dev/null || true
lsblk -f >"${STAGING}/system-info/lsblk-f.txt" 2>/dev/null || true
df -h >"${STAGING}/system-info/df-h.txt" 2>/dev/null || true
ip addr >"${STAGING}/system-info/ip-addr.txt" 2>/dev/null || true
ip route >"${STAGING}/system-info/ip-route.txt" 2>/dev/null || true
crontab -l -u trungcoolboy >"${STAGING}/system-info/crontab-trungcoolboy.txt" 2>/dev/null || true
crontab -l -u root >"${STAGING}/system-info/crontab-root.txt" 2>/dev/null || true
git -C "$REPO_DIR" remote -v >"${STAGING}/system-info/git-remotes.txt" 2>/dev/null || true
git -C "$REPO_DIR" rev-parse HEAD >"${STAGING}/system-info/git-head.txt" 2>/dev/null || true

cat >"${STAGING}/RESTORE.md" <<'EOF'
# SmartHome Restore Notes

1. Cai Ubuntu moi.
2. Cai git, python3, node/npm, mosquitto, platformio va cac package can thiet.
3. Clone repo SmartHome tu Git.
4. Copy cac file trong archive nay ve dung duong dan tuyet doi.
5. Chay `sudo systemctl daemon-reload`.
6. Enable/start cac service SmartHome can dung.

Repo khong duoc backup trong archive nay. Archive chi giu state DB, key local, service va cau hinh may.
EOF

tar -C "$WORK_DIR" -czf "$ARCHIVE" smarthome-restore
chmod 600 "$ARCHIVE"
ln -sfn "$(basename "$ARCHIVE")" "${DEST_DIR}/latest.tar.gz"

find "$DEST_DIR" -maxdepth 1 -type f -name 'smarthome-config-*.tar.gz' -printf '%T@ %p\n' \
  | sort -rn \
  | awk "NR>${KEEP_BACKUPS} {print substr(\$0, index(\$0,\$2))}" \
  | xargs -r rm -f

sync "$MOUNT_POINT" || sync
log "backup completed: ${ARCHIVE}"
