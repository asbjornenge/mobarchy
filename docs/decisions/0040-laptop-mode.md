# 0040: Laptop mode on a foldable

- Status: accepted
- Date: 2026-10-23

## Context

Half open and turned so that its fold lies across, comet looks like a small laptop. The
owner wants it to work like one: the lower half a keyboard with what a laptop's has (Ctrl,
Esc, F-keys, arrows; a 75 % Nordic layout with the big Enter), the upper half the screen.
The keyboard can't be closed then, and the lock screen's PIN is typed on it, at the top.

The hinge's angle is one of the sensors behind the Android container (ADR 0020): Android
has it as `android.sensor.hinge_angle` (type 36, "Hinge Angle", 0° closed, 180° flat).

## Decision

- **The hinge in the sensor bridge.** `mobarchy-sensord` serves it (`SENSOR_HINGE`), and
  `mobarchy-sensor-proxy` offers it beside iio-sensor-proxy's interface, which has no hinge:
  `org.mobarchy.SensorProxy.Hinge` on the same object (ClaimHinge/ReleaseHinge, HasHinge,
  HingeAngle). Claimed only while the screen is on and the panel in use folds.
- **Which panel folds, and how**, is the device's: `[[display]] fold_axis` in device.toml
  ("vertical": a book, as comet's inner panel; "horizontal": a flip phone). Generic code
  knows nothing of comet.
- **The posture** (Main.qml): laptop mode when the panel in use folds, it is turned so that
  its fold lies across (a vertical fold: a quarter turn either way), and the hinge is at
  60–150° (it stays until under 40° or over 165°, and a change waits 350 ms, so that it
  doesn't flicker on the way).
- **The keyboard** (`LaptopKeyboard.qml`): a layer of its own on the lower half (an
  exclusive zone: the apps, the drawer, quick settings and Frigg keep to the upper half),
  sliding in and out. The phone keyboard (KeyboardPanel) stays away meanwhile. Modifiers
  latch with a tap, lock with two, and hold while a finger is on them. Text goes in as the
  phone keyboard's does (text-input); keys with Ctrl, Alt or Super, and the keys that aren't
  text (F-keys, Home, PgUp, Delete, arrows), are key presses of the Norwegian keymap through
  the virtual keyboard (`Keyboard.press`). AltGr types its character.
- **The lock screen** covers the upper half (a bottom margin). While it is up the keyboard
  types into it only (Keyboard's capture): digits are the PIN (the pad shows its dots
  only), Enter unlocks. Nothing reaches the app behind.

## Consequences

- The bridge's Android half changed: a new `mobarchy-sensord` only runs after the container
  restarts (its /system/bin is an overlay that doesn't see a replaced file before then).
- Gestures stay at the bottom edge, under the keyboard.
- No touchpad (the owner's choice): the screen is touched.
