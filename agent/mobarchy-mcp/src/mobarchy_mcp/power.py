"""Battery state from the kernel's power supply class (/sys/class/power_supply)."""

from __future__ import annotations

from pathlib import Path

SUPPLIES = Path("/sys/class/power_supply")


def _read(path: Path) -> str | None:
    try:
        return path.read_text().strip()
    except OSError:
        return None


def battery(root: Path = SUPPLIES) -> dict | None:
    """The first supply of type Battery: level, charging state, and what else it reports."""
    for supply in sorted(root.iterdir()) if root.is_dir() else []:
        if _read(supply / "type") != "Battery":
            continue
        info: dict = {"name": supply.name}
        if (v := _read(supply / "capacity")) is not None:
            info["percent"] = int(v)
        if (v := _read(supply / "status")) is not None:
            info["status"] = v  # Charging, Discharging, Full, Not charging
        if (v := _read(supply / "health")) is not None:
            info["health"] = v
        if (v := _read(supply / "temp")) is not None:
            info["temperature_c"] = int(v) / 10  # tenths of a degree
        if (v := _read(supply / "time_to_empty_avg")) is not None and int(v) > 0:
            info["minutes_to_empty"] = int(v) // 60
        if (v := _read(supply / "time_to_full_avg")) is not None and int(v) > 0:
            info["minutes_to_full"] = int(v) // 60
        return info
    return None
