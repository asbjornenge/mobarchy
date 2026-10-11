"""Alarms and timers (ADR 0028), as a file the shell follows and rings from:

  ~/.local/share/mobarchy/alarms.json

  {"alarms": [{"id", "time": "HH:MM", "days": [1..7] (ISO weekdays, Monday 1; none: once),
               "label", "enabled", "created": epoch s, "snoozed_until": epoch s | null,
               "last_rung": epoch s | null}],
   "timers": [{"id", "label", "ends": epoch s, "duration_s"}]}

The agent (these functions) and the Clock app write it; the shell schedules and rings, and
writes back what ringing changes (last_rung, snoozed_until; a one-off alarm turns itself off,
a timer that has rung goes). Writes replace the file whole (rename), so the shell never reads
half of one, and a change is made under a lock on alarms.json.lock (flock, as the shell does),
so the two don't undo each other's."""

from __future__ import annotations

import contextlib
import datetime as dt
import fcntl
import json
import os
import re
import secrets
import time
from pathlib import Path

DAY_NAMES = ["mon", "tue", "wed", "thu", "fri", "sat", "sun"]
GROUPS = {"weekdays": [1, 2, 3, 4, 5], "weekend": [6, 7], "daily": [1, 2, 3, 4, 5, 6, 7]}
TIME = re.compile(r"^\s*(\d{1,2})[:.](\d{2})\s*$")
MAX_TIMER_S = 7 * 24 * 3600


class AlarmError(Exception):
    pass


def path() -> Path:
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "mobarchy/alarms.json"


@contextlib.contextmanager
def changing():
    """load(), change it, then save(): with the lock held throughout."""
    lock = path().with_name("alarms.json.lock")
    lock.parent.mkdir(parents=True, exist_ok=True)
    with open(lock, "a") as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        data = load()
        yield data
        save(data)


def load() -> dict:
    try:
        data = json.loads(path().read_text())
    except FileNotFoundError:
        data = {}
    except (OSError, ValueError) as e:
        raise AlarmError(f"cannot read {path()}: {e}") from e
    return {"alarms": list(data.get("alarms", [])), "timers": list(data.get("timers", []))}


def save(data: dict) -> None:
    p = path()
    p.parent.mkdir(parents=True, exist_ok=True)
    tmp = p.with_name(p.name + ".tmp")
    tmp.write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n")
    os.replace(tmp, p)


def parse_time(text: str) -> str:
    m = TIME.match(text)
    if not m or int(m[1]) > 23 or int(m[2]) > 59:
        raise AlarmError(f"not a time of day (HH:MM, 24 h): {text!r}")
    return f"{int(m[1]):02d}:{m[2]}"


def parse_days(days: list[str] | None) -> list[int]:
    """["mon", "wed"], ["weekdays"], ["daily"], ["weekend"] -> ISO weekdays; none: once."""
    out: set[int] = set()
    for d in days or []:
        key = d.strip().lower()
        if key in GROUPS:
            out.update(GROUPS[key])
        elif key[:3] in DAY_NAMES and key.isalpha():
            out.add(DAY_NAMES.index(key[:3]) + 1)
        else:
            raise AlarmError(f"not a day: {d!r} (mon..sun, weekdays, weekend, daily)")
    return sorted(out)


def next_ring(alarm: dict, now: dt.datetime) -> dt.datetime | None:
    """When an alarm rings next, after now (local, naive); a snooze comes first."""
    if not alarm.get("enabled", True):
        return None
    snoozed = alarm.get("snoozed_until")
    if snoozed and snoozed > now.timestamp():
        return dt.datetime.fromtimestamp(snoozed)
    hh, mm = map(int, alarm["time"].split(":"))
    days = alarm.get("days") or []
    for ahead in range(8):
        day = now.date() + dt.timedelta(days=ahead)
        at = dt.datetime.combine(day, dt.time(hh, mm))
        if at <= now:
            continue
        if not days or day.isoweekday() in days:
            return at
    return None


def describe_days(days: list[int]) -> str:
    if not days:
        return "once"
    for name, group in GROUPS.items():
        if days == group:
            return name
    return ",".join(DAY_NAMES[d - 1] for d in days)


def _alarm_view(a: dict, now: dt.datetime) -> dict:
    nxt = next_ring(a, now)
    return {"id": a["id"], "time": a["time"], "days": describe_days(a.get("days") or []),
            "label": a.get("label", ""), "enabled": a.get("enabled", True),
            "next": nxt.isoformat(timespec="minutes") if nxt else None,
            "snoozed": bool(a.get("snoozed_until") and a["snoozed_until"] > now.timestamp())}


def _timer_view(t: dict, now: float) -> dict:
    return {"id": t["id"], "label": t.get("label", ""), "duration_s": t.get("duration_s"),
            "ends": dt.datetime.fromtimestamp(t["ends"]).isoformat(timespec="seconds"),
            "remaining_s": max(0, round(t["ends"] - now))}


def list_all(now: dt.datetime | None = None) -> dict:
    now = now or dt.datetime.now()
    data = load()
    return {"now": now.isoformat(timespec="minutes"),
            "alarms": sorted((_alarm_view(a, now) for a in data["alarms"]), key=lambda a: a["time"]),
            "timers": [_timer_view(t, now.timestamp()) for t in data["timers"]]}


def _find(items: list[dict], ident: str, what: str) -> dict:
    for item in items:
        if item["id"] == ident:
            return item
    raise AlarmError(f"no {what} with id {ident!r} (list them first)")


def add_alarm(time_of_day: str, label: str = "", days: list[str] | None = None,
              now: dt.datetime | None = None) -> dict:
    now = now or dt.datetime.now()
    alarm = {"id": secrets.token_hex(3), "time": parse_time(time_of_day), "days": parse_days(days),
             "label": label.strip()[:80], "enabled": True, "created": now.timestamp(),
             "snoozed_until": None, "last_rung": None}
    with changing() as data:
        data["alarms"].append(alarm)
    return _alarm_view(alarm, now)


def update_alarm(ident: str, time_of_day: str | None = None, label: str | None = None,
                 days: list[str] | None = None, enabled: bool | None = None,
                 now: dt.datetime | None = None) -> dict:
    now = now or dt.datetime.now()
    new_time = parse_time(time_of_day) if time_of_day is not None else None
    new_days = parse_days(days) if days is not None else None
    with changing() as data:
        alarm = _find(data["alarms"], ident, "alarm")
        if new_time is not None:
            alarm["time"] = new_time
        if label is not None:
            alarm["label"] = label.strip()[:80]
        if new_days is not None:
            alarm["days"] = new_days
        if enabled is not None:
            alarm["enabled"] = enabled
        # (changed: it counts from now, and a snooze is over)
        alarm["created"] = now.timestamp()
        alarm["snoozed_until"] = None
    return _alarm_view(alarm, now)


def delete_alarm(ident: str) -> dict:
    with changing() as data:
        data["alarms"].remove(_find(data["alarms"], ident, "alarm"))
    return {"deleted": ident}


def start_timer(duration_s: int, label: str = "", now: float | None = None) -> dict:
    now = time.time() if now is None else now
    if not 1 <= duration_s <= MAX_TIMER_S:
        raise AlarmError(f"a timer runs from 1 s to {MAX_TIMER_S // 86400} days, not {duration_s} s")
    timer = {"id": secrets.token_hex(3), "label": label.strip()[:80], "ends": now + duration_s,
             "duration_s": duration_s}
    with changing() as data:
        data["timers"].append(timer)
    return _timer_view(timer, now)


def cancel_timer(ident: str) -> dict:
    with changing() as data:
        data["timers"].remove(_find(data["timers"], ident, "timer"))
    return {"cancelled": ident}
