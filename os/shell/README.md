# mobarchy-shell

The phone UI ([ADR 0009](../../docs/decisions/0009-shell-qt-quick.md)): Qt Quick on layer-shell.

| Window | Kind | What |
|---|---|---|
| `StatusBar.qml` | layer, top | clock, wifi, battery; swipe down (or tap): quick settings |
| `Edge.qml` | layer (bottom layer, so it stays under the keyboard), bottom edge | swipe up: app drawer; tap: home; ⌨ toggles the keyboard |
| `Drawer.qml` | layer, overlay, mapped while open | home and open apps (switch, close), installed apps |
| `QuickSettings.qml` | layer, overlay, mapped while open | time and battery, brightness, tiles (Wi-Fi, flight mode, keyboard; mobile network, sound and Bluetooth shown as not there yet), restart / power off; a Wi-Fi page with the networks in range |
| `WifiPassword.qml` | normal window | the passphrase for a new network (text input needs a normal window) |
| `LockScreen.qml` | layer, overlay, full screen | clock, date, battery, notification count; swipe up to unlock (no PIN yet) |
| `Banner.qml` | layer, overlay, top, mapped while shown | a new notification for a few seconds |
| `Home.qml` | normal window | the agent: feed and Ask field (text input needs a normal window, ADR 0006) |

C++ (`src/`) holds what QML can't do well: desktop entries and app launching (`apps`,
`desktopentry`), the window list over wlr-foreign-toplevel-management (`toplevels`), the
agent process (`agent`), squeekboard over D-Bus (`keyboard`), battery/wifi (`systemstatus`), and
brightness and power through systemd-logind (`logind`; brightness goes to the backlights of
the panels that are on, so no device knowledge is needed), Wi-Fi over wpa_supplicant's
control socket (`wifi`, `wpaparse`), radios through /dev/rfkill (`rfkill`), the screen and
the hardware keys (`screenpower`: wlr-output-power-management, ext-idle-notify, evdev
power and volume keys), icons (`icontheme`: the freedesktop icon theme lookup, thread-safe for Qt
Quick's loader threads), `device.toml` (`device`, `deviceinfo`), and the notification
server (`notifications`: org.freedesktop.Notifications on the session bus; banners, a list
in quick settings, actions).

Power key: a short press turns the screen off (and locks) or on; a long press opens quick
settings. Volume keys: 5 % steps (held: repeating), with a short volume bar. The screen also goes off after a minute without input, and a woken lock screen
that is not touched goes dark again after ten seconds.

`Sheet.qml` is the sliding layer surface behind the drawer and quick settings, and
`EdgeGesture.qml` is the drag that drives a sheet (distance and release velocity).

Layer surfaces come back by themselves when their output goes away (fold): see `LayerWindow.qml`.

## Build and test

    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
    ctest --test-dir build

Package (what the image installs): `tools/pkg-build os/shell --out <dir>`, then
`os/image/build ... --extra packages=<dir>`. On a device with base-devel, from a copy of this
directory: `BUILDDIR=/tmp/makepkg makepkg` (see the note in `PKGBUILD`).

The simulator builds and installs it on `sim/mobsim sync`.

## Running

`mobarchy-shell.service` (systemd user unit), started by the Hyprland session. Environment:

- `MOBARCHY_AGENT_BIN`: the agent (default: `/opt/mobarchy/agent/mobarchy-agent/.venv/bin/mobarchy-agent`)
- `MOBARCHY_TERMINAL`: for `Terminal=true` apps (default: `foot`)
- `MOBARCHY_ICON_THEME`: icon theme (default: `Adwaita`)

## Performance notes

- Views get `currentIndex: -1`. Otherwise ListView/GridView animate an invisible highlight
  and render at full frame rate for seconds.
- Touch compression is off (`QML_NO_TOUCH_COMPRESSION`, set in `main.cpp`). With it on, every
  touch move schedules a frame on the window under the finger, and the drawer drops to half
  the refresh rate.
- Nothing animates unless the user or a real event drives it: the shell idles at ~0 % CPU.
- Icons are loaded asynchronously. The provider must be thread-safe: an earlier QIcon-based
  one crashed in the SVG icon engine when called from a loader thread.
