# 0028: Alarms and timers, rung by the shell

- Status: accepted (the Clock app to come)
- Date: 2026-10-11

## Context

The owner wants alarms on the phone, and the agent (Frigg, the assistant) should be able
to set them: "wake me at 7", "ten minutes for the pasta". There was nothing to build on:
the shell played no sound, there was no ringing screen, no clock app.

The phone never suspends (sleep targets are masked; "sleep" only turns the screen off), so
an alarm needs no RTC wake-up: something running has to notice the time and ring. The
shell is always running (systemd restarts it within a second), owns the screen, the lock
screen, the haptics and the volume.

## Decision

**The alarms are a file, `~/.local/share/mobarchy/alarms.json`; the shell rings them.**

```json
{"alarms": [{"id", "time": "07:30", "days": [1, 2, 3, 4, 5], "label", "enabled",
             "created", "snoozed_until", "last_rung"}],
 "timers": [{"id", "label", "ends", "duration_s"}]}
```

Times of day are local; days are ISO weekdays (Monday 1, none: once); the rest are epoch
seconds.

- **Who writes it.** The agent's tools (mobarchy-mcp `alarms.py`: `alarm_set`, `alarm_list`,
  `alarm_update`, `alarm_delete`, `timer_start`, `timer_cancel`) and, later, the Clock app.
  The shell writes back what ringing changes: `last_rung` (an alarm doesn't ring twice for
  the same time, also across a restart), `snoozed_until`, a one-off alarm turns itself off
  when stopped, a timer that has rung goes. Every change is read-change-write under
  `flock(alarms.json.lock)`, the whole file replaced by a rename: no half-written file, no
  lost update between the agent and the shell. The same pattern as the themes (ADR 0025):
  the agent writes a file, the shell follows it.
- **Scheduling** (`alarmschedule.{h,cpp}`, unit-tested): the shell waits for the next ring,
  but at most a minute (the clock or the zone may change meanwhile). A ring that came while
  the shell was down rings late, up to 10 minutes; not if it came before the alarm was made
  or changed (`created`).
- **Ringing** (`AlarmScreen.qml`): wakes the screen and keeps it on, over the lock screen:
  the time, the label, "Snooze 9 min" and "Stop". A chime made for Mobarchy
  (`data/sounds/make-alarm.py`, no licence to carry) that grows from soft to full in 30 s,
  and a long buzz every 1.6 s (device.toml `[haptics] buzz_*`: on comet the Pixel's long
  vibration waveform). A muted or quiet phone is turned up to half for it, and back after.
  The power key snoozes. After 10 minutes it stops by itself. Several due at once ring one
  after the other.
- **Shown:** the next alarm under the date on the lock screen, and an alarm icon in the
  status bar while one is set.

Alarms are owner-only (the agent's `ACT` tools, not for a guest voice on a locked phone):
their labels can be personal.

## Consequences

- The shell has to be running to ring. It is (the session restarts it), and a ring missed
  in a restart comes late rather than never.
- If Mobarchy gets real suspend, the shell has to set an RTC wake alarm for the next ring
  first (a system helper: the session has no `CAP_WAKE_ALARM`).
- A Clock app (list, add, edit, timers) writes the same file; until then, alarms are set
  and changed through the agent.
