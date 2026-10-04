# 0020: The sensors on the Linux side, and screen rotation

- Status: accepted
- Date: 2026-10-04

## Context

On comet the sensors (accelerometer, light, proximity, the hinge angle) sit behind the AOC
and are reached only through the vendor's sensor HAL, in the Android container. Android's
sensorservice runs there now, for the container's HALs (ADR 0017: the camera HAL's gyro).
The Linux side has none of them:
- The screen doesn't follow how the phone is held.
- Photos don't know which way is up.
- Automatic brightness and the proximity sensor (screen off during a call) need them later.

## Decision

- **A sensor bridge in the container** (`os/android-sensors`, `mobarchy-sensord`), in the
  same pattern as the camera bridge (ADR 0018). It is a small NDK program that reads the
  sensors through Android's sensor API, as an app would. It serves them on a Unix socket in
  the container's /data, and runs a sensor only while the Linux side asks for it.
  - The device's orientation comes from Android's own **device orientation sensor**: the
    debounced orientation Android itself rotates the screen by.
  - Also offered: ambient light, proximity and the raw accelerometer.
- **iio-sensor-proxy's interface on the Linux side** (`mobarchy-sensor-proxy`, on the
  system bus as `net.hadess.SensorProxy`).
  - It offers AccelerometerOrientation (normal, left-up, right-up, bottom-up), LightLevel
    in lux, and ProximityNear.
  - Sensors run only while claimed, and a client that leaves the bus releases its claims,
    as with iio-sensor-proxy.
  - What already reads that interface works unchanged: desktop shells, Qt Sensors and
    rotation helpers.
- **Rotation in the shell**:
  - The shell claims the orientation while the screen is on and the panel in use isn't
    locked. It then calls the device's rotation hook (device.toml `[rotation] hook`) with
    the orientation. Generic code doesn't know how a device's panels and touch are mounted.
  - On comet the hook is the fold script. It rewrites the monitor rules (the panel's
    transform) and the inner panel's touch transform, and reloads Hyprland. A fold goes
    back to upright.
  - A rotation lock per panel lives in quick settings and is remembered. device.toml says
    which panels start locked: comet's outer panel stays upright until unlocked.
- **Photos** take their orientation from the device orientation itself, not from the
  screen: a photo held sideways is sideways even with rotation locked.

## Consequences

- Automatic brightness and proximity (calls) can build on the same proxy.
- How a foldable's inner panel relates to the orientation Android reports may need a
  per-panel offset. That is the fold script's business (comet), not the shell's.
- The proxy conflicts with iio-sensor-proxy on devices whose sensors Linux reads
  directly; those use iio-sensor-proxy itself, and the shell works with either.
