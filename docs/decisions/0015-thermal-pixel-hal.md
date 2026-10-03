# 0015: Thermal management through Pixel's thermal HAL

- Status: accepted
- Date: 2026-10-03

## Context

Compiling on the comet while it charged overheated it: charger_therm passed its critical
trip (64 °C) and the hardware protection shut the phone down.

On Tensor G4, the CPU and GPU junction limits live in firmware (ACPM's governor), which
is active. Everything above that is the job of Android's thermal HAL:
- It computes "virtual skin" temperatures from the board's thermistors.
- It throttles the CPUs (frequency and uclamp) and the GPU, and limits the charging
  current (chg_mdis), with PID loops.
- Its thresholds and gains are tuned by Google per device, in
  vendor/etc/thermal_info_config*.json.

Without it, nothing limits skin or charger temperature.

## Decision

- **Pixel's own thermal HAL** (android.hardware.thermal-service.pixel) runs in the
  Android container (ADR 0014), with Google's comet configuration. Mobarchy doesn't write
  its own thermal policy for a device whose vendor has tuned one.
- **The GPU is taken out of the HAL's control.** The configuration names Mali's own
  driver's cooling device (thermal-gpufreq-0); Mobarchy drives the GPU with panthor
  (ADR 0008). The HAL refuses to start with a missing cooling device.
  - A device overlay hook (devices/comet/rootfs/usr/lib/mobarchy/android-overlay)
    generates the configuration without it, from the vendor's copy, at every container
    start.
  - The GPU keeps the firmware's junction limit.
- As on Android, `/dev/thermal/{tz,cdev}-by-name` are made by the vendor's
  thermal_symlinks. The trip points and cooling devices the HAL writes are given to
  `system`, as the vendor's rc files do.

## Consequences

- Thermal protection depends on the Android container running: it has to start at boot,
  for every comet, phone service or not.
- Measured (4 busy cores, charging at 15 W, 7 min):
  - skin held at 39–40 °C and charger_therm at about 45 °C;
  - charging was cut back to almost nothing (chg_mdis 25/26), and the big cores capped;
  - without the HAL, charger_therm reached 49 °C in 45 s.
- The HAL's power hints go nowhere (no power HAL); they only tune performance.
