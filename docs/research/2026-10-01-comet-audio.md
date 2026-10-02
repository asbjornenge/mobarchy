# comet audio: where it stands (2026-10-01)

Pixel 9 Pro Fold audio runs through Google's AoC ("Always-on Compute"), a DSP that also
handles sensors (CHRE), hotword and the touch bus negotiator (TBN). The application
processor talks to it over mailboxes. The ALSA card (`google,aoc-snd-card`) only comes up
once the AoC firmware runs.

## What was found

- The kernel side is already in place: `aoc_core`, `aoc_alsa_dev`, `aoc_alsa_dev_util`, the
  `google-aoc-snd-*` platform drivers and the speaker amps (`snd_soc_cs35l41` ×2, `cs40l26`
  haptics) load at boot. `/lib/firmware/aoc.bin` and the Cirrus amp firmware are present.
- Without anything else, AoC stays `Offline` (`/sys/devices/platform/17000000.aoc/services`),
  and the card's probe is deferred (`platform sound-aoc: deferred probe pending`).
- On Android, init starts AoC by writing the firmware name to sysfs. The driver
  (`private/google-modules/aoc/aoc.c`, `firmware_store` → `start_firmware_load`) loads it
  with `request_firmware_nowait` and authenticates it through GSA (`gsa_load_aoc_fw_image`).
  There is also an `aoc_autoload_firmware` module parameter.

Doing that by hand on Mobarchy worked on the first try:

    echo aoc.bin > /sys/devices/platform/17000000.aoc/firmware

    aoc: successfully loaded firmware version 16047897-polygon type release
    aoc 17000000.aoc: Loading signed aoc image
    gsa 16490000.gsa-ns: TZ: com.android.trusty.gsa.hwmgr.aoc connected
    aoc: firmware version 16047897-polygon did become online with 94 services
    gti: tbn: aoc_channel_kthread: AOC TBN service is ready.

`/proc/asound/cards` then shows `aoc-snd-card` with 35 PCM devices (EP1–EP8 playback,
captures, voip, incall, raw, haptics, ...) and 486 mixer controls.

## Routing (from the device's own Android configuration)

The Android audio HAL drives the card through `/vendor/etc/mixer_paths.xml` and
`/vendor/etc/audio_platform_configuration.xml` (read from vendor_a, mounted read-only via a
read-only device-mapper table over `super`; the files are proprietary and not in this repo).

- Primary playback is AoC endpoint EP2 (`pcmC0D1p`, "EP2 playback").
- `primary-playback -> speaker`: `TDM_0_RX Mixer EP2 = 1`.
- `speaker` device path: `Main AMP Enable Switch = 1` and `R Main AMP Enable Switch = 1`,
  plus Cirrus "fast use case" switches.
- Defaults at the top of mixer_paths.xml: TDM_0 4 channels S32_LE 48 kHz, amp slot
  positions, DRE on, `AMP PCM Gain = 17` for both amps.
- Two speakers, both cs35l41 ("" and "R " prefixed controls), with speaker-protection DSP
  firmware (`cs35l41-dsp1-spk-prot.wmfw/.bin`) and calibration
  (`R-cs35l41-dsp1-spk-cali.bin`, `cs35l41-dsp1-spk-cali.*`).

## Not done yet, and why

No sound has been played. The speaker amps run Cirrus speaker-protection firmware that
needs per-device calibration (on Android it comes from the persist partition). Driving the
amps without it, or with the wrong calibration, can overheat the small speakers. That needs
a deliberate, supervised first test at low volume.

## First playback attempt (2026-10-02)

Set up as the HAL would: mixer defaults plus the `speaker`, `adapted-speaker` and
`primary-playback -> speaker` paths from mixer_paths.xml, amp protection firmware loaded
(`DSP1 Preload Switch`), and calibration written to `DSP1 Protection cd CAL_R / CAL_STATUS /
CAL_CHECKSUM / CAL_AMBIENT` (both amps). The calibration comes from
`/mnt/vendor/persist/audio/speaker.cal`: per amp six little-endian u32, of which the first
four are R, status, checksum (R + status) and ambient temperature; the firmware's own
default CAL_AMBIENT (23) matches. Amp level −20 dB (`Digital PCM Volume` 657; Android uses 817 = 0 dB).

`aplay -D hw:0,1` (EP2) fails after ~100 ms with -EIO: the PCM position never moves.
- AoC reads exactly one ring buffer's worth (16 KB, `audio_playback1` Tx/Rx in
  `.../17000000.aoc/services`) and then stops reading. The playback chain inside AoC does
  not run; the stuck position is a symptom.
- AoC's PCM mailbox interrupts do not arrive either. Patch
  `devices/comet/kernel/patches/private/google-modules/aoc/0001` lets the driver poll the
  position with its timer instead (module parameter `aoc_alsa_dev_util.pcm_timer`, on by
  default); it does not help while AoC is not consuming.
- Linux 6.1 ALSA times out a stalled stream after 100 ms (not 10 s as older kernels).

Probably missing: whatever the Android audio HAL (closed source) tells AoC to start the
output (commands through the `audio_output_control` service or controls not in mixer_paths).
The way to learn it: boot Android (slot a) and record mixer changes and AoC commands
during playback.

## Next steps

1. A comet unit that starts AoC at boot (`devices/comet/rootfs`), after confirming that touch
   still works with AoC online (TBN now negotiates the touch bus with AoC).
2. Load the amp protection firmware with the device's calibration (find where Android keeps
   it; read it from persist read-only).
3. An ALSA UCM profile for the card (HiFi: speaker via EP2, mic captures), so PipeWire /
   WirePlumber can use it like any other card.
4. Volume and mute in quick settings once PipeWire is up.
