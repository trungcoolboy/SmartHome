#!/usr/bin/env python3
import json
import os
import signal
import subprocess
import sys
import time
from logging.handlers import RotatingFileHandler
from pathlib import Path


SAMPLE_INTERVAL_SECONDS = 5.0
DETAIL_COOLDOWN_SECONDS = 60.0
LOAD1_THRESHOLD = 6.0
LOAD5_THRESHOLD = 5.0
MEM_AVAILABLE_MIN_KB = 512 * 1024
PROCESS_RSS_ALERT_KB = 600 * 1024
PROCESS_RSS_WARN_KB = 350 * 1024
API_RSS_WARN_KB = 250 * 1024


def now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def read_proc_key_values(path: str) -> dict[str, int]:
    data: dict[str, int] = {}
    try:
      with open(path, "r", encoding="utf-8") as handle:
        for raw_line in handle:
            parts = raw_line.replace(":", "").split()
            if len(parts) >= 2 and parts[1].isdigit():
                data[parts[0]] = int(parts[1])
    except OSError:
        return data
    return data


def read_pressure(resource: str) -> dict[str, float]:
    result: dict[str, float] = {}
    path = f"/proc/pressure/{resource}"
    try:
        with open(path, "r", encoding="utf-8") as handle:
            for raw_line in handle:
                line = raw_line.strip()
                if not line:
                    continue
                key, *fields = line.split()
                for field in fields:
                    if "=" not in field:
                        continue
                    metric, value = field.split("=", 1)
                    try:
                        result[f"{key}_{metric}"] = float(value)
                    except ValueError:
                        continue
    except OSError:
        return result
    return result


def read_net_dev() -> dict[str, dict[str, int]]:
    stats: dict[str, dict[str, int]] = {}
    try:
        with open("/proc/net/dev", "r", encoding="utf-8") as handle:
            lines = handle.readlines()[2:]
    except OSError:
        return stats

    for raw_line in lines:
        if ":" not in raw_line:
            continue
        iface, payload = raw_line.split(":", 1)
        fields = payload.split()
        if len(fields) < 16:
            continue
        stats[iface.strip()] = {
            "rx_bytes": int(fields[0]),
            "rx_packets": int(fields[1]),
            "tx_bytes": int(fields[8]),
            "tx_packets": int(fields[9]),
        }
    return stats


def run_command(args: list[str], timeout_seconds: float = 5.0) -> str:
    try:
        completed = subprocess.run(
            args,
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout_seconds,
        )
    except Exception as exc:
        return f"<error: {exc}>"
    output = completed.stdout.strip()
    if completed.stderr.strip():
        output = f"{output}\n[stderr]\n{completed.stderr.strip()}".strip()
    return output


def collect_process_table(sort_key: str) -> list[dict[str, object]]:
    output = run_command(
        [
            "ps",
            "-eo",
            "pid,ppid,comm,%cpu,%mem,rss,stat",
            f"--sort={sort_key}",
        ],
        timeout_seconds=3.0,
    )
    rows: list[dict[str, object]] = []
    lines = [line for line in output.splitlines() if line.strip()]
    if len(lines) <= 1:
        return rows
    for line in lines[1:9]:
        parts = line.split(None, 6)
        if len(parts) != 7:
            continue
        try:
            rows.append(
                {
                    "pid": int(parts[0]),
                    "ppid": int(parts[1]),
                    "comm": parts[2],
                    "cpu_percent": float(parts[3]),
                    "mem_percent": float(parts[4]),
                    "rss_kb": int(parts[5]),
                    "stat": parts[6],
                }
            )
        except ValueError:
            continue
    return rows


def find_process_rss_kb(process_rows: list[dict[str, object]], names: tuple[str, ...]) -> int:
    for row in process_rows:
        if str(row.get("comm")) in names:
            return int(row.get("rss_kb", 0))
    return 0


def collect_sample() -> dict[str, object]:
    meminfo = read_proc_key_values("/proc/meminfo")
    uptime_seconds = 0.0
    try:
        with open("/proc/uptime", "r", encoding="utf-8") as handle:
            uptime_seconds = float(handle.read().split()[0])
    except Exception:
        uptime_seconds = 0.0

    load1, load5, load15 = os.getloadavg()
    top_cpu = collect_process_table("-pcpu")
    top_rss = collect_process_table("-rss")
    api_rss_kb = find_process_rss_kb(top_rss, ("python3",))

    sample = {
        "ts": now_iso(),
        "epoch": time.time(),
        "uptime_seconds": round(uptime_seconds, 3),
        "loadavg": {"1m": load1, "5m": load5, "15m": load15},
        "mem_kb": {
            "total": meminfo.get("MemTotal", 0),
            "free": meminfo.get("MemFree", 0),
            "available": meminfo.get("MemAvailable", 0),
            "buffers": meminfo.get("Buffers", 0),
            "cached": meminfo.get("Cached", 0),
            "swap_total": meminfo.get("SwapTotal", 0),
            "swap_free": meminfo.get("SwapFree", 0),
        },
        "pressure": {
            "cpu": read_pressure("cpu"),
            "io": read_pressure("io"),
            "memory": read_pressure("memory"),
        },
        "net": read_net_dev(),
        "top_cpu": top_cpu,
        "top_rss": top_rss,
        "process_count": len([name for name in os.listdir("/proc") if name.isdigit()]),
        "smart_home_api_rss_kb": api_rss_kb,
    }
    return sample


def reason_for_detail(sample: dict[str, object]) -> list[str]:
    reasons: list[str] = []
    loadavg = sample["loadavg"]
    mem_kb = sample["mem_kb"]
    if loadavg["1m"] >= LOAD1_THRESHOLD:
        reasons.append(f"load1={loadavg['1m']:.2f}")
    if loadavg["5m"] >= LOAD5_THRESHOLD:
        reasons.append(f"load5={loadavg['5m']:.2f}")
    if mem_kb["available"] <= MEM_AVAILABLE_MIN_KB:
        reasons.append(f"mem_available_kb={mem_kb['available']}")
    if sample["smart_home_api_rss_kb"] >= API_RSS_WARN_KB:
        reasons.append(f"api_rss_kb={sample['smart_home_api_rss_kb']}")
    for row in sample["top_rss"]:
        if int(row.get("rss_kb", 0)) >= PROCESS_RSS_ALERT_KB:
            reasons.append(f"process_rss:{row.get('comm')}={row.get('rss_kb')}")
            break
    return reasons


def write_detail_snapshot(detail_dir: Path, sample: dict[str, object], reasons: list[str]) -> None:
    timestamp = time.strftime("%Y%m%d-%H%M%S", time.gmtime())
    snapshot_path = detail_dir / f"detail-{timestamp}.json"
    payload = {
        "sample": sample,
        "reasons": reasons,
        "vmstat": run_command(["vmstat", "1", "3"], timeout_seconds=5.0),
        "iostat": run_command(["iostat", "-xz", "1", "2"], timeout_seconds=5.0),
        "ss": run_command(["ss", "-tanp"], timeout_seconds=5.0),
        "smart_home_services": run_command(
            ["systemctl", "status", "smart-home-api.service", "smart-home-dashboard.service"],
            timeout_seconds=5.0,
        ),
        "journal_services": run_command(
            [
                "journalctl",
                "-n",
                "120",
                "-u",
                "smart-home-api.service",
                "-u",
                "smart-home-dashboard.service",
            ],
            timeout_seconds=5.0,
        ),
        "dmesg_tail": run_command(["dmesg"], timeout_seconds=5.0).splitlines()[-120:],
    }
    snapshot_path.write_text(json.dumps(payload, ensure_ascii=True, indent=2), encoding="utf-8")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: system_watchdog.py <log-dir>", file=sys.stderr)
        return 2

    log_dir = Path(sys.argv[1])
    detail_dir = log_dir / "details"
    log_dir.mkdir(parents=True, exist_ok=True)
    detail_dir.mkdir(parents=True, exist_ok=True)

    jsonl_path = log_dir / "samples.jsonl"
    rotating_log = RotatingFileHandler(log_dir / "watchdog.log", maxBytes=2_000_000, backupCount=4)
    rotating_log.setFormatter(None)

    stop_requested = False

    def handle_signal(signum, _frame):
        nonlocal stop_requested
        stop_requested = True

    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)

    last_detail_epoch = 0.0

    while not stop_requested:
        started = time.time()
        sample = collect_sample()
        with jsonl_path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(sample, ensure_ascii=True))
            handle.write("\n")

        rotating_log.stream.write(
            f"{sample['ts']} load1={sample['loadavg']['1m']:.2f} "
            f"mem_available_kb={sample['mem_kb']['available']} "
            f"api_rss_kb={sample['smart_home_api_rss_kb']}\n"
        )
        rotating_log.flush()

        reasons = reason_for_detail(sample)
        now_epoch = time.time()
        if reasons and (now_epoch - last_detail_epoch) >= DETAIL_COOLDOWN_SECONDS:
            write_detail_snapshot(detail_dir, sample, reasons)
            last_detail_epoch = now_epoch

        elapsed = time.time() - started
        sleep_seconds = max(0.5, SAMPLE_INTERVAL_SECONDS - elapsed)
        time.sleep(sleep_seconds)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
