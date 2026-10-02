"""Bluetooth through BlueZ's bluetoothctl; the radio's on/off switch is rfkill, the same
switch as the shell's tile and flight mode (a device's bring-up follows it)."""

from __future__ import annotations

import re
import subprocess

ADDRESS = re.compile(r"^[0-9A-F]{2}(:[0-9A-F]{2}){5}$")


def _ctl(*args: str, timeout: float = 15) -> str:
    r = subprocess.run(["bluetoothctl", *args], capture_output=True, text=True, timeout=timeout)
    return r.stdout


def parse_devices(text: str) -> dict[str, str]:
    """`bluetoothctl devices` lines ("Device <address> <name>") -> {address: name}."""
    out = {}
    for line in text.splitlines():
        parts = line.split(" ", 2)
        if len(parts) == 3 and parts[0] == "Device" and ADDRESS.match(parts[1]):
            out[parts[1]] = parts[2]
    return out


def parse_info(text: str) -> dict[str, str]:
    """`bluetoothctl info|show` -> its "Key: value" fields (the first of repeated keys)."""
    fields: dict[str, str] = {}
    for line in text.splitlines():
        if line.startswith("\t") and ": " in line:
            key, value = line.strip().split(": ", 1)
            fields.setdefault(key, value)
    return fields


def _rfkill_on() -> bool | None:
    r = subprocess.run(["rfkill", "--noheadings", "--output", "TYPE,SOFT,HARD"], capture_output=True, text=True)
    states = [line.split() for line in r.stdout.splitlines() if line.startswith("bluetooth")]
    if not states:
        return None
    return all(s[1] == "unblocked" and s[2] == "unblocked" for s in states)


def _device(address: str, name: str) -> dict:
    info = parse_info(_ctl("info", address))
    d = {"address": address, "name": info.get("Alias", name), "paired": info.get("Paired") == "yes",
         "connected": info.get("Connected") == "yes"}
    if "Icon" in info:
        d["kind"] = info["Icon"]
    if "Battery Percentage" in info:  # "0x50 (80)"
        d["battery_percent"] = int(info["Battery Percentage"].split("(")[-1].rstrip(")"))
    return d


def status() -> dict:
    on = _rfkill_on()
    if on is None:
        return {"available": False}
    controller = parse_info(_ctl("show")) if on else {}
    devices = [_device(a, n) for a, n in parse_devices(_ctl("devices", "Paired")).items()] if controller else []
    return {"available": True, "enabled": on, "powered": controller.get("Powered") == "yes",
            "name": controller.get("Alias"), "paired_devices": devices}


def scan(seconds: int) -> list[dict]:
    """Devices in range that tell their name (unnamed ones are beacons and phones' random
    addresses), paired ones included."""
    _ctl("--timeout", str(seconds), "scan", "on", timeout=seconds + 10)
    return [_device(a, n) for a, n in parse_devices(_ctl("devices")).items()
            if n.replace("-", ":") != a]


def _check(address: str) -> None:
    if not ADDRESS.match(address):
        raise ValueError(f"not a Bluetooth address: {address!r} (e.g. 00:1A:7D:DA:71:13)")


def _run(action: str, address: str, timeout: float = 40) -> str:
    out = _ctl("--timeout", str(int(timeout)), action, address, timeout=timeout + 5)
    return out.strip().splitlines()[-1] if out.strip() else ""


def connect(address: str) -> dict:
    """Pair and trust first if needed (just-works pairing), then connect."""
    _check(address)
    if not parse_info(_ctl("info", address)):
        raise ValueError(f"unknown device {address}: scan first, with the device in pairing mode")
    if parse_info(_ctl("info", address)).get("Paired") != "yes":
        result = _run("pair", address)
        if "successful" not in result.lower():
            raise RuntimeError(f"pairing failed: {result}")
        _run("trust", address, 10)
    result = _run("connect", address)
    if "successful" not in result.lower():
        raise RuntimeError(f"connecting failed: {result}")
    return _device(address, address)


def disconnect(address: str) -> dict:
    _check(address)
    _run("disconnect", address, 15)
    return _device(address, address)


def forget(address: str) -> str:
    _check(address)
    return _run("remove", address, 15)


def set_enabled(enabled: bool) -> dict:
    subprocess.run(["rfkill", "unblock" if enabled else "block", "bluetooth"], check=True)
    return {"enabled": enabled}
