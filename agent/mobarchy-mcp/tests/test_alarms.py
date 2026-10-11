import datetime as dt
import json

import pytest

from mobarchy_mcp import alarms

# A Sunday morning
NOW = dt.datetime(2026, 10, 11, 9, 30)


@pytest.fixture(autouse=True)
def data_home(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_DATA_HOME", str(tmp_path))
    return tmp_path


def test_times_and_days_are_parsed():
    assert alarms.parse_time("7:05") == "07:05"
    assert alarms.parse_time("23.59") == "23:59"
    for bad in ("24:00", "7", "7:60", "seven"):
        with pytest.raises(alarms.AlarmError):
            alarms.parse_time(bad)
    assert alarms.parse_days(["weekdays"]) == [1, 2, 3, 4, 5]
    assert alarms.parse_days(["Saturday", "sun", "sat"]) == [6, 7]
    assert alarms.parse_days([]) == []
    with pytest.raises(alarms.AlarmError):
        alarms.parse_days(["someday"])


def test_a_one_off_alarm_rings_at_its_next_time():
    later = alarms.add_alarm("10:00", "Coffee", now=NOW)
    assert later["next"] == "2026-10-11T10:00" and later["days"] == "once"
    earlier = alarms.add_alarm("07:00", now=NOW)
    assert earlier["next"] == "2026-10-12T07:00"


def test_a_repeating_alarm_skips_to_its_days():
    work = alarms.add_alarm("07:00", "Work", ["weekdays"], now=NOW)
    assert work["next"] == "2026-10-12T07:00"   # Monday
    sat = alarms.add_alarm("08:00", days=["sat"], now=NOW)
    assert sat["next"] == "2026-10-17T08:00"


def test_the_file_is_what_the_shell_reads(data_home):
    a = alarms.add_alarm("06:45", "Run", ["mon", "wed"], now=NOW)
    raw = json.loads((data_home / "mobarchy/alarms.json").read_text())
    stored = raw["alarms"][0]
    assert stored["id"] == a["id"] and stored["time"] == "06:45" and stored["days"] == [1, 3]
    assert stored["enabled"] is True and stored["created"] == NOW.timestamp()
    assert raw["timers"] == []


def test_update_disable_and_delete():
    a = alarms.add_alarm("07:00", now=NOW)
    off = alarms.update_alarm(a["id"], enabled=False, now=NOW)
    assert off["enabled"] is False and off["next"] is None
    moved = alarms.update_alarm(a["id"], time_of_day="9:45", enabled=True, now=NOW)
    assert moved["next"] == "2026-10-11T09:45"
    alarms.delete_alarm(a["id"])
    assert alarms.list_all(NOW)["alarms"] == []
    with pytest.raises(alarms.AlarmError):
        alarms.delete_alarm(a["id"])


def test_a_snooze_comes_first():
    a = alarms.add_alarm("07:00", days=["daily"], now=NOW)
    data = alarms.load()
    data["alarms"][0]["snoozed_until"] = NOW.timestamp() + 540
    alarms.save(data)
    assert alarms.list_all(NOW)["alarms"][0]["next"] == "2026-10-11T09:39"
    # changing the alarm ends the snooze
    assert alarms.update_alarm(a["id"], label="x", now=NOW)["next"] == "2026-10-12T07:00"


def test_timers():
    t = alarms.start_timer(600, "Pasta", now=NOW.timestamp())
    assert t["remaining_s"] == 600 and t["ends"] == "2026-10-11T09:40:00"
    assert alarms.list_all(NOW)["timers"][0]["label"] == "Pasta"
    alarms.cancel_timer(t["id"])
    assert alarms.list_all(NOW)["timers"] == []
    with pytest.raises(alarms.AlarmError):
        alarms.start_timer(0)
