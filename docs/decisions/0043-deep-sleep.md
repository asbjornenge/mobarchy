# 0043: Deep sleep

- Status: accepted (on since 2026-10-23 evening; calls in sleep still to be tried with a SIM)
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
  "android"`), and runs an APF program (Android Packet Filter, `wifi-apf`; `wifi_apf =
  true`; installed through nl80211's vendor command with Google's OUI, as Android's Wi-Fi
  HAL does): frames for us, ARP and IPv6 router/neighbour messages wake the phone, other
  broadcasts and multicasts and 802.3/LLC frames don't. Verified against Google's APF
  interpreter (hardware/google/apf) before it went to the chip (APF 6.1, 4 KB). Both off
  again with the screen on.
- **The modem told the screen is off:** oFono's binder plugin sets the RIL's device state
  and indication filter (no signal strength reports and the like) from MCE's display
  state; Mobarchy has no MCE, so `mobarchy-mce` (root) offers that much of its D-Bus face
  (com.nokia.mce: display from the shell's `$XDG_RUNTIME_DIR/mobarchy/display`, charger
  and battery from the power supply class). Without it the modem woke the phone every few
  seconds (17 of 38 wakes in the first test, the rest Wi-Fi).
- **Short wakes for packets:** after a Wi-Fi wake the grace is 1 s, not 5.
- **Never asleep:** with the hotspot on, or `/run/mobarchy/stay-awake` (development).

## Consequences

- "Hei Frigg" keeps the phone awake while it listens (no DSP hotword): the owner's
  choice in Frigg's settings (always, or while charging) is also a battery choice.
- Background work (mail sync, calendars) runs while the phone is awake anyway; nothing
  wakes it on a schedule yet.
- Asleep, the phone doesn't answer on the network at all (the chip drops the broadcasts,
  ARP among them): to reach it over ssh, wake it, and `touch /run/mobarchy/stay-awake`.
- To watch: calls and messages arriving while asleep (the modem's wakeup source must hold
  until ofono and the shell have them), Bluetooth audio, and how often Wi-Fi still wakes it.

## First trials (2026-10-23)

- Power key, a timer ringing out of sleep: work.
- Sleeps were short: the modem (17 of 38 wakes: hence mobarchy-mce) and Wi-Fi packets (21;
  with SETSUSPENDMODE on, broadcasts and multicasts still woke it: 44 and 11 of 111 in one
  count) woke it within a second or so.
- After the first long sleep (7 min) the Wi-Fi chip didn't answer: the driver's first
  command timed out ("Treating IOVAR timeout as PCIe linkdown"), and Wi-Fi was dead until
  switched off and on. Before it the suspend loop had tried many times a second while the
  chip was busy, with its PCIe link in recovery (`RCVRY LOCK`): the loop now backs off
  (0.1 s doubling to a minute). Until the Wi-Fi resumes reliably (and filters what it
  should), the service stays off; the shell's SleepGate and mobarchy-mce stay in.

## Second round (2026-10-23 evening)

- With the backoff, Wi-Fi survived 3 and 7 minutes of sleeping (on and off): no linkdown.
- oFono's binder plugin got MCE's display state (mobarchy-mce) and told the modem "low
  data expected", but always set the indication filter to all: patched
  (`os/packages/ofono-binder-plugin`, pkgrel 2): none with the display off, as Android.
  The modem's wakes went from 104 in 7 min to none.
- Measured with the battery's charge counter: **109 mA** on average with the screen off
  (5 min), against 175 mA awake. What is left is Wi-Fi (138 wakes in 5 min). The packets
  that woke it, by a sniffer comparing the wall and monotonic clocks: non-IP broadcasts and
  multicasts (Realtek's RRCP, ethertype 0x8899, from the home router; 802.3/LLC frames),
  ZeroTier (UDP 9993), and connections to the internet. Next: drop non-IP broadcasts in
  the chip's packet filter, and fewer keepalives.
- With APF: **90 mA** (5 min). What still woke it (~130 times in 5 min) was almost all
  ZeroTier (UDP 9993 and 59956: the development PC on the same network and ZeroTier's
  roots, every few seconds): a developer's cost, measured next without it.
- Without ZeroTier (stopped for the measurement): 125 mA, still 109 Wi-Fi wakes in 5 min.
  Its peers keep sending to its port, so stopping it changes nothing while they do; and
  the charge counter is coarse (steps of ~0.8 mAh: about ±10 mA over 5 minutes), so these
  numbers are rough. Where it stands: 175 mA awake, ~90–110 mA asleep when it can. What is
  left: unicast traffic from open connections (ZeroTier, apps' keepalives); next, longer
  keepalives while asleep and background apps frozen, as Android does.
- The Wi-Fi chip sometimes does not answer after a wake (twice on 2026-10-24): bcmdhd
  times out on an IOVAR, calls it a PCIe link down and sends its hang event; the interface
  stays up and "connected" to the last network with every scan failing, until Wi-Fi is
  switched off and on. Android's Wi-Fi service restarts Wi-Fi on that event; here
  `wifi-watchdog` does: the driver's line (device.toml `[wifi] hang_log`) in the kernel's
  log turns Wi-Fi off and on (rfkill, as its switch), under a 20 s wake lock, if it was on.
  The suspend daemon applies SETSUSPENDMODE and APF again after any link change (the
  restarted chip, a new association, forgets them).
- A link up that carries nothing (seen after a roam between a mesh network's access points):
  `wifi-watchdog` asks for the gateway (ARP) every 10 s with the screen on, as Android's
  IpReachabilityMonitor; no answer for 30 s: reassociate; 60 s: Wi-Fi off and on. ZeroTier
  is restarted on a new network (address or gateway) and after a recovery.
- Tried and rejected: `dhd_disable_l2_in_d3=1` (no PCIe L2 in D3): the chip still hung, and
  one resume ended in a hard lockup and a kernel panic (HOST_SLEEP-EXIT TIMEOUT). Never set.
- No deep sleep at all after some boots (2026-10-25/26): the debug initramfs forces the USB
  controller's B-session valid (comet's usb_post_bind, so a host reaches its shell). Left set,
  the Exynos DWC3 glue holds its `dwc3-otg` wakeup source until a data host is unplugged; a
  charger never clears it. `mobarchy-usb-release` (comet) gives the session back to the
  Type-C port 15 s after boot when no host is using it; from then on the TCPC sets and
  clears it as on Android.
