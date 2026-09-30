"""The device description (devices/<codename>/device.toml), installed on the device."""

from __future__ import annotations

import os
import tomllib
from pathlib import Path

DEFAULT_DEVICE_PATH = Path("/etc/mobarchy/device.toml")


def load_device(path: Path | None = None) -> dict:
    path = path or Path(os.environ.get("MOBARCHY_DEVICE", DEFAULT_DEVICE_PATH))
    if not path.exists():
        return {"device": {"codename": "unknown", "name": "Unknown device"}, "display": []}
    with path.open("rb") as f:
        return tomllib.load(f)
