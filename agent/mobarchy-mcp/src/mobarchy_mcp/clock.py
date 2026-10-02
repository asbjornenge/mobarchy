"""Date, time and time zone through systemd-timedated (timedatectl).

The image's polkit rule (/etc/polkit-1/rules.d/50-mobarchy-timedate.rules) lets the owner
(group wheel) change the time zone and network time without a password prompt, which the
agent has no way to answer.
"""

from __future__ import annotations

import subprocess


def _timedatectl(*args: str) -> str:
    r = subprocess.run(["timedatectl", "--no-ask-password", *args], capture_output=True,
                       text=True, timeout=10)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.strip() or r.stdout.strip() or f"timedatectl {args[0]} failed")
    return r.stdout


def parse_show(text: str) -> dict:
    """`timedatectl show` output (key=value lines) -> the fields an agent needs."""
    s = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
    return {
        "timezone": s.get("Timezone"),
        "network_time": s.get("NTP") == "yes",
        "synchronized": s.get("NTPSynchronized") == "yes",
    }


def status() -> dict:
    info = parse_show(_timedatectl("show"))
    # Local time as the system shows it, with the zone's offset and abbreviation.
    info["local_time"] = subprocess.run(["date", "+%Y-%m-%d %H:%M:%S %Z (UTC%:z)"],
                                        capture_output=True, text=True).stdout.strip()
    return info


def timezones() -> list[str]:
    return _timedatectl("list-timezones").split()


def match_timezones(query: str, zones: list[str]) -> list[str]:
    """Zones containing every word of the query (case-insensitive; spaces match '_')."""
    words = query.lower().replace("_", " ").split()
    return [z for z in zones if all(w in z.lower().replace("_", " ") for w in words)]


def set_timezone(zone: str) -> dict:
    zones = timezones()
    if zone not in zones:
        close = match_timezones(zone.split("/")[-1], zones)[:10]
        hint = f" Did you mean: {', '.join(close)}?" if close else ""
        raise ValueError(f"unknown time zone '{zone}'.{hint}")
    _timedatectl("set-timezone", zone)
    return status()


def set_network_time(enabled: bool) -> dict:
    _timedatectl("set-ntp", "true" if enabled else "false")
    return status()
