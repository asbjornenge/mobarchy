# 0043: Deep sleep

- Status: accepted (first version; to be tuned with use)
- Date: 2026-10-23

## Context

The phone never suspended: the sleep targets were masked and logind told to ignore the
power key and the lid (ADR 0028 relied on it: an alarm needed no RTC wake). With the screen
off it drew about 175 mA (0.7 W, ~25 h standby); a phone in deep sleep draws a tenth of
that or less. comet's kernel suspends and resumes fine (tested 2026-10-23 with pm_test up to
`core`, then a real `mem` suspend woken by the RTC): the only driver that refuses is the
Wi-Fi's (bcmdhd: -EBUSY while it holds its rx/tx wake locks, i.e. just after traffic). The
kernel has wakeup sources and `/sys/power/wakeup_count`, but no `autosleep`.

## Decision

Android's way, in userspace:

- **mobarchy-suspend** (root, `/usr/lib/mobarchy/suspend`, `mobarchy-suspend.service`)
  suspends to RAM whenever it may: the wakeup_count handshake (reading it waits while a
  kernel wakeup source is active; writing it back refuses a suspend after an event since),
  then `mem` into /sys/power/state; a Wi-Fi still busy is tried again a moment later.
- **The shell decides when it may** (`SleepGate`): the screen is off and there is no call,
  no ringing, no alarm ringing, nothing playing, no conversation with Frigg, no "Hei
  Frigg" listening, no app keeping the screen on. It says so with a file in its runtime
  directory (`$XDG_RUNTIME_DIR/mobarchy/sleep-ok`); no shell, no file: no sleep.
- **Waking:** what the kernel wakes for (the modem's call or message, the power key, the
  fingerprint sensor, the charger, a packet for us), and the RTC for the shell's next alarm
  or timer (`next-wake` beside sleep-ok; 30 s early). After each wake a grace of 5 s: the
  event reaches the shell, which turns the screen on (and sleep-ok goes) if it should.
- **Wi-Fi while asleep may be:** the chip filters what it wakes the phone for (Android's
  private driver command `SETSUSPENDMODE`; device.toml `[power] wifi_suspend_mode =
  "android"`), off again with the screen on.
- **Never asleep:** with the hotspot on, or `/run/mobarchy/stay-awake` (development).

## Consequences

- "Hei Frigg" keeps the phone awake while it listens (no DSP hotword): the owner's
  choice in Frigg's settings (always, or while charging) is also a battery choice.
- Background work (mail sync, calendars) runs while the phone is awake anyway; nothing
  wakes it on a schedule yet.
- Over ssh (ZeroTier on Wi-Fi) a sleeping phone answers late or not at all: stay-awake.
- To watch: calls and messages arriving while asleep (the modem's wakeup source must hold
  until ofono and the shell have them), Bluetooth audio, and how often Wi-Fi still wakes it.
