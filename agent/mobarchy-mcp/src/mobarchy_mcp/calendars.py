"""The owner's calendars (ADR 0034): the phone's own and the accounts' (CalDAV, synced by
mobarchy-accounts), read as one. Events are iCalendar (RFC 5545); repeating ones are expanded
here (recurring-ical-events), so whoever asks gets each occurrence.

  ~/.local/share/mobarchy/calendar/phone.ics      the phone's own calendar (written here)
  ~/.cache/mobarchy/caldav/<account id>.json      the accounts' events, as synced
  ~/.config/mobarchy/accounts.json                which of their calendars are on, and colours

`mobarchy-calendar`, the command (the Calendar app's and the home board's backend), prints
JSON."""

from __future__ import annotations

import argparse
import contextlib
import datetime as dt
import fcntl
import json
import os
import sys
import uuid
from pathlib import Path

import icalendar
import recurring_ical_events

PHONE = "phone"
PHONE_NAME = "Phone"
PHONE_COLOR = "#7aa2f7"


class CalendarError(Exception):
    pass


def _xdg(var: str, default: str) -> Path:
    return Path(os.environ.get(var, Path.home() / default))


def phone_file() -> Path:
    return _xdg("XDG_DATA_HOME", ".local/share") / "mobarchy/calendar/phone.ics"


def caldav_cache() -> Path:
    return _xdg("XDG_CACHE_HOME", ".cache") / "mobarchy/caldav"


def accounts_file() -> Path:
    return _xdg("XDG_CONFIG_HOME", ".config") / "mobarchy/accounts.json"


def local_tz() -> dt.tzinfo:
    """The phone's time zone by name (Europe/Oslo), so a repeating event keeps its hour over
    summer time; a fixed offset only without one."""
    from zoneinfo import ZoneInfo, ZoneInfoNotFoundError
    name = os.environ.get("TZ", "").lstrip(":")
    if not name:
        try:
            name = os.readlink("/etc/localtime").split("zoneinfo/", 1)[1]
        except (OSError, IndexError):
            name = ""
    try:
        return ZoneInfo(name) if name else dt.datetime.now().astimezone().tzinfo
    except (ZoneInfoNotFoundError, ValueError):
        return dt.datetime.now().astimezone().tzinfo


@contextlib.contextmanager
def locked():
    p = phone_file()
    p.parent.mkdir(parents=True, exist_ok=True)
    with open(f"{p}.lock", "w") as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        yield


# --- the calendars --------------------------------------------------------------------------

def _phone_calendar() -> icalendar.Calendar:
    try:
        return icalendar.Calendar.from_ical(phone_file().read_bytes())
    except (FileNotFoundError, ValueError):
        cal = icalendar.Calendar()
        cal.add("prodid", "-//Mobarchy//Calendar//EN")
        cal.add("version", "2.0")
        return cal


def _save_phone(cal: icalendar.Calendar) -> None:
    with contextlib.suppress(Exception):     # (the zones its times are in, for other readers)
        cal.add_missing_timezones()
    p = phone_file()
    p.parent.mkdir(parents=True, exist_ok=True)
    tmp = p.with_suffix(".ics.tmp")
    tmp.write_bytes(cal.to_ical())
    os.replace(tmp, p)


def calendars() -> list[dict]:
    """Every calendar there is: {id, name, color, account, enabled, editable}."""
    out = [{"id": PHONE, "name": PHONE_NAME, "color": PHONE_COLOR, "account": "", "enabled": True,
            "editable": True}]
    try:
        accounts = json.loads(accounts_file().read_text()).get("accounts", [])
    except (FileNotFoundError, json.JSONDecodeError):
        accounts = []
    for a in accounts:
        for url, c in a.get("calendars", {}).items():
            out.append({"id": f"{a['id']}|{url}", "name": c.get("name", ""), "color": c.get("color") or PHONE_COLOR,
                        "account": a.get("user", ""), "enabled": c.get("enabled", True), "editable": False})
    return out


def _sources() -> list[tuple[dict, list[icalendar.Calendar]]]:
    """(calendar, its iCalendar objects), for the enabled ones."""
    out = []
    by_id = {c["id"]: c for c in calendars()}
    out.append((by_id[PHONE], [_phone_calendar()]))
    caches = {}
    for c in by_id.values():
        if c["id"] == PHONE or not c["enabled"]:
            continue
        account, url = c["id"].split("|", 1)
        if account not in caches:
            try:
                caches[account] = json.loads((caldav_cache() / f"{account}.json").read_text())
            except (FileNotFoundError, json.JSONDecodeError):
                caches[account] = {}
        objects = []
        for item in caches[account].get(url, {}).values():
            try:
                objects.append(icalendar.Calendar.from_ical(item["ics"]))
            except ValueError:
                continue
        out.append((c, objects))
    return out


# --- events ---------------------------------------------------------------------------------

def _as_datetime(value, tz) -> tuple[dt.datetime, bool]:
    """(an aware datetime in the local zone, all day)"""
    if isinstance(value, dt.datetime):
        if value.tzinfo is None:
            value = value.replace(tzinfo=tz)
        return value.astimezone(tz), False
    return dt.datetime.combine(value, dt.time(), tz), True


def _event(ev, cal: dict, tz) -> dict:
    start, all_day = _as_datetime(ev.get("dtstart").dt, tz)
    if ev.get("dtend") is not None:
        end, _ = _as_datetime(ev.get("dtend").dt, tz)
    elif ev.get("duration") is not None:
        end = start + ev.get("duration").dt
    else:
        end = start + (dt.timedelta(days=1) if all_day else dt.timedelta(0))
    uid = str(ev.get("uid", ""))
    rid = ev.get("recurrence-id")
    return {"id": uid + ("@" + _as_datetime(rid.dt, tz)[0].isoformat() if rid else "@" + start.isoformat()),
            "uid": uid, "calendar": cal["id"], "calendar_name": cal["name"], "color": cal["color"],
            "title": str(ev.get("summary", "")) or "(no title)", "start": start.isoformat(),
            "end": end.isoformat(), "all_day": all_day, "location": str(ev.get("location", "")),
            "notes": str(ev.get("description", "")), "editable": cal["editable"]}


def events(start: dt.datetime, end: dt.datetime) -> list[dict]:
    """Every occurrence between start and end, in time order (all-day first on their day)."""
    tz = local_tz()
    out = []
    for cal, objects in _sources():
        for obj in objects:
            try:
                found = recurring_ical_events.of(obj).between(start, end)
            except Exception:      # (one broken event does not hide the rest)
                continue
            for ev in found:
                if str(ev.get("status", "")).upper() == "CANCELLED":
                    continue
                out.append(_event(ev, cal, tz))
    out.sort(key=lambda e: (e["start"][:10], not e["all_day"], e["start"], e["title"]))
    return out


def _parse_when(text: str, tz) -> tuple[dt.datetime | dt.date, bool]:
    """"2026-10-20" (all day) or "2026-10-20T14:00" (local time, or with an offset)."""
    text = text.strip()
    if len(text) == 10:
        return dt.date.fromisoformat(text), True
    value = dt.datetime.fromisoformat(text)
    return (value if value.tzinfo else value.replace(tzinfo=tz)), False


def add_event(title: str, start: str, end: str = "", location: str = "", notes: str = "",
              repeat: str = "") -> dict:
    """An event in the phone's calendar. start/end: "YYYY-MM-DD" for all day, or
    "YYYY-MM-DDTHH:MM". end defaults to an hour on (a day for all day). repeat: an RRULE
    ("FREQ=WEEKLY;BYDAY=MO") or "daily", "weekly", "monthly", "yearly"."""
    if not title.strip():
        raise CalendarError("an event needs a title")
    tz = local_tz()
    begin, all_day = _parse_when(start, tz)
    if end:
        finish, end_all_day = _parse_when(end, tz)
        if end_all_day != all_day:
            raise CalendarError("start and end must both be dates, or both times")
    else:
        finish = begin + (dt.timedelta(days=1) if all_day else dt.timedelta(hours=1))
    if all_day and finish <= begin:
        finish = begin + dt.timedelta(days=1)
    if finish < begin:
        raise CalendarError("the end is before the start")
    ev = icalendar.Event()
    uid = f"{uuid.uuid4()}@mobarchy"
    ev.add("uid", uid)
    ev.add("dtstamp", dt.datetime.now(dt.timezone.utc))
    ev.add("summary", title.strip())
    ev.add("dtstart", begin)
    ev.add("dtend", finish)
    if location:
        ev.add("location", location)
    if notes:
        ev.add("description", notes)
    if repeat:
        simple = {"daily": "FREQ=DAILY", "weekly": "FREQ=WEEKLY", "monthly": "FREQ=MONTHLY",
                  "yearly": "FREQ=YEARLY"}.get(repeat.lower(), repeat)
        try:
            ev.add("rrule", icalendar.vRecur.from_ical(simple))
        except ValueError as e:
            raise CalendarError(f"repeat: not a rule ({simple})") from e
    with locked():
        cal = _phone_calendar()
        cal.add_component(ev)
        _save_phone(cal)
    return {"added": uid, "title": title.strip(), "start": str(begin), "end": str(finish)}


def _phone_event(uid: str, cal: icalendar.Calendar):
    for ev in cal.walk("VEVENT"):
        if str(ev.get("uid")) == uid:
            return ev
    raise CalendarError(f"no event {uid!r} in the phone's calendar (account calendars are read-only)")


def update_event(uid: str, title: str | None = None, start: str | None = None, end: str | None = None,
                 location: str | None = None, notes: str | None = None) -> dict:
    tz = local_tz()
    with locked():
        cal = _phone_calendar()
        ev = _phone_event(uid, cal)
        if title is not None:
            ev["summary"] = icalendar.vText(title)
        if start is not None:
            old_start = ev.get("dtstart").dt
            old_end = ev.get("dtend").dt if ev.get("dtend") is not None else old_start
            begin, _ = _parse_when(start, tz)
            ev.pop("dtstart")
            ev.add("dtstart", begin)
            if end is None:     # (moved: the same length)
                ev.pop("dtend", None)
                ev.add("dtend", begin + (old_end - old_start))
        if end is not None:
            finish, _ = _parse_when(end, tz)
            ev.pop("dtend", None)
            ev.add("dtend", finish)
        for key, value in (("location", location), ("description", notes)):
            if value is not None:
                ev.pop(key, None)
                if value:
                    ev.add(key, value)
        _save_phone(cal)
    return {"updated": uid}


def delete_event(uid: str) -> dict:
    with locked():
        cal = _phone_calendar()
        ev = _phone_event(uid, cal)
        cal.subcomponents.remove(ev)
        _save_phone(cal)
    return {"deleted": uid}


# --- the command ----------------------------------------------------------------------------

def _day(text: str) -> dt.datetime:
    return dt.datetime.combine(dt.date.fromisoformat(text), dt.time(), local_tz())


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="mobarchy-calendar", description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("events", help="the occurrences from a day, for some days")
    e.add_argument("--from", dest="start", default=dt.date.today().isoformat())
    e.add_argument("--days", type=int, default=1)
    sub.add_parser("calendars")
    a = sub.add_parser("add")
    a.add_argument("title")
    a.add_argument("start")
    a.add_argument("--end", default="")
    a.add_argument("--location", default="")
    a.add_argument("--notes", default="")
    a.add_argument("--repeat", default="")
    u = sub.add_parser("update")
    u.add_argument("uid")
    for f in ("title", "start", "end", "location", "notes"):
        u.add_argument(f"--{f}")
    d = sub.add_parser("delete")
    d.add_argument("uid")
    a_ = p.parse_args(argv)
    try:
        if a_.cmd == "events":
            begin = _day(a_.start)
            out = {"events": events(begin, begin + dt.timedelta(days=max(1, min(a_.days, 400))))}
        elif a_.cmd == "calendars":
            out = {"calendars": calendars()}
        elif a_.cmd == "add":
            out = add_event(a_.title, a_.start, a_.end, a_.location, a_.notes, a_.repeat)
        elif a_.cmd == "update":
            out = update_event(a_.uid, a_.title, a_.start, a_.end, a_.location, a_.notes)
        else:
            out = delete_event(a_.uid)
    except (CalendarError, ValueError) as err:
        print(json.dumps({"error": str(err)}), flush=True)
        return 1
    print(json.dumps(out, ensure_ascii=False), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
