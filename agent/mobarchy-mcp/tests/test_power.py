from mobarchy_mcp import power


def test_battery(tmp_path):
    (tmp_path / "usb").mkdir()
    (tmp_path / "usb" / "type").write_text("USB\n")
    b = tmp_path / "battery"
    b.mkdir()
    for name, value in {"type": "Battery", "capacity": "87", "status": "Charging", "health": "Good",
                        "temp": "312", "time_to_full_avg": "1800", "time_to_empty_avg": "0"}.items():
        (b / name).write_text(value + "\n")
    assert power.battery(tmp_path) == {"name": "battery", "percent": 87, "status": "Charging",
                                       "health": "Good", "temperature_c": 31.2, "minutes_to_full": 30}


def test_no_battery(tmp_path):
    assert power.battery(tmp_path) is None
