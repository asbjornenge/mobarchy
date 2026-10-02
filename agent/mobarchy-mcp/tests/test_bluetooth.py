from mobarchy_mcp import bluetooth

DEVICES = """Device 5E:67:C4:00:00:01 5E-67-C4-00-00-01
Device A6:D7:3C:00:00:02 ET-2820 Series
Device 00:1A:7D:DA:71:13 Owner’s AirPods
Waiting to connect to bluetoothd...
"""

INFO = """Device 00:1A:7D:DA:71:13 (public)
\tName: Owner’s AirPods
\tAlias: Owner’s AirPods
\tIcon: audio-headphones
\tPaired: yes
\tConnected: yes
\tUUID: Audio Sink                (0000110b-0000-1000-8000-00805f9b34fb)
\tUUID: Handsfree                 (0000111e-0000-1000-8000-00805f9b34fb)
\tBattery Percentage: 0x50 (80)
"""


def test_parse_devices():
    assert bluetooth.parse_devices(DEVICES) == {
        "5E:67:C4:00:00:01": "5E-67-C4-00-00-01",
        "A6:D7:3C:00:00:02": "ET-2820 Series",
        "00:1A:7D:DA:71:13": "Owner’s AirPods",
    }


def test_parse_info():
    info = bluetooth.parse_info(INFO)
    assert info["Paired"] == "yes" and info["Icon"] == "audio-headphones"
    assert info["UUID"].startswith("Audio Sink")  # the first of repeated keys
    assert info["Battery Percentage"] == "0x50 (80)"


def test_device(monkeypatch):
    monkeypatch.setattr(bluetooth, "_ctl", lambda *a, **k: INFO)
    assert bluetooth._device("00:1A:7D:DA:71:13", "x") == {
        "address": "00:1A:7D:DA:71:13", "name": "Owner’s AirPods", "paired": True,
        "connected": True, "kind": "audio-headphones", "battery_percent": 80}


def test_bad_address():
    try:
        bluetooth.connect("airpods")
    except ValueError as e:
        assert "not a Bluetooth address" in str(e)
    else:
        raise AssertionError("expected ValueError")
