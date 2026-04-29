# Smart Home Watchdog

This watchdog writes a lightweight system sample every 5 seconds to:

- `/var/log/smarthome-watchdog/samples.jsonl`

When the machine looks unhealthy, it also writes a heavier detail snapshot to:

- `/var/log/smarthome-watchdog/details/`

Triggers:

- load average too high
- memory available too low
- `smart-home-api` RSS too high
- any process RSS too high

Install:

1. copy `smarthome-watchdog.service` to `/etc/systemd/system/`
2. copy `99-smarthome-debug.conf` to `/etc/sysctl.d/`
3. `systemctl daemon-reload`
4. `systemctl enable --now smarthome-watchdog.service`
5. `sysctl --system`

Useful files after a freeze:

- `/var/log/smarthome-watchdog/samples.jsonl`
- `/var/log/smarthome-watchdog/details/*.json`
