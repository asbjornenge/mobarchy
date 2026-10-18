import datetime as dt
import json

import pytest

from mobarchy_mcp import calendars


@pytest.fixture
def home(tmp_path, monkeypatch):
    for var in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"):
        monkeypatch.setenv(var, str(tmp_path / var))
    monkeypatch.setattr(calendars, "local_tz", lambda: dt.timezone(dt.timedelta(hours=2)))
    return tmp_path


def day(s):
    return dt.datetime.combine(dt.date.fromisoformat(s), dt.time(), dt.timezone(dt.timedelta(hours=2)))


def test_add_and_read(home):
    calendars.add_event("Tannlege", "2026-10-23T14:00", location="Storgata 1")
    calendars.add_event("Fødselsdag", "2026-10-23")
    got = calendars.events(day("2026-10-23"), day("2026-10-24"))
    assert [e["title"] for e in got] == ["Fødselsdag", "Tannlege"]       # all day first
    tooth = got[1]
    assert tooth["start"] == "2026-10-23T14:00:00+02:00" and tooth["end"] == "2026-10-23T15:00:00+02:00"
    assert tooth["location"] == "Storgata 1" and tooth["editable"] and not tooth["all_day"]
    assert got[0]["all_day"]


def test_repeating(home):
    calendars.add_event("Trening", "2026-10-19T18:00", repeat="weekly")
    got = calendars.events(day("2026-10-19"), day("2026-11-09"))
    assert [e["start"][:10] for e in got] == ["2026-10-19", "2026-10-26", "2026-11-02"]


def test_update_and_delete(home):
    uid = calendars.add_event("Møte", "2026-10-20T09:00", end="2026-10-20T09:30")["added"]
    calendars.update_event(uid, title="Standup", start="2026-10-20T10:00")
    [ev] = calendars.events(day("2026-10-20"), day("2026-10-21"))
    assert ev["title"] == "Standup" and ev["end"] == "2026-10-20T10:30:00+02:00"
    calendars.delete_event(uid)
    assert calendars.events(day("2026-10-20"), day("2026-10-21")) == []


def test_account_calendars(home):
    (home / "XDG_CONFIG_HOME/mobarchy").mkdir(parents=True)
    (home / "XDG_CONFIG_HOME/mobarchy/accounts.json").write_text(json.dumps({"accounts": [
        {"id": "google-me", "user": "me@x", "calendars": {
            "https://cal/1/": {"name": "Jobb", "color": "#ff0000", "enabled": True},
            "https://cal/2/": {"name": "Off", "color": "#00ff00", "enabled": False}}}]}))
    ics = ("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:a1\r\nSUMMARY:Lansering\r\n"
           "DTSTART:20261021T080000Z\r\nDTEND:20261021T090000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n")
    (home / "XDG_CACHE_HOME/mobarchy/caldav").mkdir(parents=True)
    (home / "XDG_CACHE_HOME/mobarchy/caldav/google-me.json").write_text(json.dumps({
        "https://cal/1/": {"/1/a1.ics": {"etag": "e", "ics": ics}},
        "https://cal/2/": {"/2/b.ics": {"etag": "e", "ics": ics.replace("a1", "b1")}}}))
    [ev] = calendars.events(day("2026-10-21"), day("2026-10-22"))
    assert ev["title"] == "Lansering" and ev["color"] == "#ff0000" and not ev["editable"]
    assert ev["start"] == "2026-10-21T10:00:00+02:00"
    with pytest.raises(calendars.CalendarError):
        calendars.delete_event("a1")


def test_bad_input(home):
    with pytest.raises(calendars.CalendarError):
        calendars.add_event("", "2026-10-20")
    with pytest.raises(calendars.CalendarError):
        calendars.add_event("x", "2026-10-20T10:00", end="2026-10-20T09:00")
