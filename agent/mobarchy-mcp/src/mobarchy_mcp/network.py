"""Wifi through wpa_supplicant's control interface (wpa_cli), as set up by the image
(wpa_supplicant@wlan0, ctrl_interface group wheel; DHCP by systemd-networkd).

Credentials go straight to wpa_supplicant, which keeps them on the device.
"""

from __future__ import annotations

import subprocess

IFACE = "wlan0"


def _cli(*args: str, timeout: float = 10) -> str:
    r = subprocess.run(["wpa_cli", "-i", IFACE, *args], capture_output=True, text=True,
                       timeout=timeout)
    if r.returncode != 0:
        raise RuntimeError(f"wpa_cli {' '.join(args)}: {r.stderr.strip() or r.stdout.strip()}")
    return r.stdout


def parse_status(text: str) -> dict:
    """`wpa_cli status` output (key=value lines) -> dict."""
    return dict(line.split("=", 1) for line in text.splitlines() if "=" in line)


def parse_scan_results(text: str) -> list[dict]:
    """`wpa_cli scan_results` output -> one entry per SSID (strongest BSS), strongest first."""
    best: dict[str, dict] = {}
    for line in text.splitlines()[1:]:
        fields = line.split("\t")
        if len(fields) < 5 or not fields[4]:
            continue
        bssid, freq, signal, flags, ssid = fields[:5]
        entry = {"ssid": ssid, "signal_dbm": int(signal), "freq_mhz": int(freq),
                 "secured": any(k in flags for k in ("WPA", "WEP", "SAE"))}
        if ssid not in best or entry["signal_dbm"] > best[ssid]["signal_dbm"]:
            best[ssid] = entry
    return sorted(best.values(), key=lambda e: e["signal_dbm"], reverse=True)


def addresses() -> list[str]:
    r = subprocess.run(["ip", "-brief", "address", "show", IFACE], capture_output=True, text=True)
    # "wlan0 UP 192.168.1.2/24 metric 600 fe80::1/64": keep the addresses only.
    return [f for f in r.stdout.split()[2:] if "/" in f]


def status() -> dict:
    s = parse_status(_cli("status"))
    return {
        "interface": IFACE,
        "state": s.get("wpa_state", "UNKNOWN"),
        "ssid": s.get("ssid"),
        "freq_mhz": int(s["freq"]) if "freq" in s else None,
        "addresses": addresses(),
    }


def scan(wait_s: float = 4) -> list[dict]:
    import time

    _cli("scan")
    time.sleep(wait_s)
    return parse_scan_results(_cli("scan_results"))


def connect(ssid: str, passphrase: str | None, wait_s: float = 20) -> dict:
    import time

    net = _cli("add_network").strip().splitlines()[-1]
    _cli("set_network", net, "ssid", f'"{ssid}"')
    if passphrase:
        _cli("set_network", net, "psk", f'"{passphrase}"')
    else:
        _cli("set_network", net, "key_mgmt", "NONE")
    _cli("enable_network", net)
    _cli("select_network", net)
    _cli("save_config")
    deadline = time.monotonic() + wait_s
    while time.monotonic() < deadline:
        if parse_status(_cli("status")).get("wpa_state") == "COMPLETED":
            break
        time.sleep(1)
    return status()
