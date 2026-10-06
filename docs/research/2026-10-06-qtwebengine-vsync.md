# QtWebEngine on a 120 Hz screen: Chromium's frames off the display's clock (2026-10-06)

Mobarchy's browser (ADR 0023) scrolled with a judder on comet (Pixel 9 Pro Fold, 120 Hz
panels), while the Qt Quick UI around it was smooth.

## Cause

Chromium's frames are begun by viz's synthetic BeginFrame source. It runs at
`BeginFrameArgs::DefaultInterval()` (60 Hz) unless the display's vsync parameters reach it
(`DisplayPrivate::SetDisplayVSyncParameters`, from the browser's `ui::Compositor`).
QtWebEngine never sets them. QtWebEngine's output device (NativeSkiaOutputDevice) also
acknowledges swaps without a presentation interval, so the source never learns the
display's rate.

The result: Chromium makes frames at 60 Hz, out of phase with the window, and on a 120 Hz
screen they land one, two or three vsyncs apart. QScreen reports 120 Hz, and Qt's own
render loop runs at 8.33 ms; only `screen_info.display_frequency` gets the rate, for the
web page.

The GPU and CPU were not the limit: Chromium's threads used ≤ 11 % CPU during a scroll,
and the Mali GPU sat at 940 MHz.

## Fix

`os/packages/qt6-webengine/0100-Chromium-draws-in-step-with-the-Qt-window.patch`, 44 lines
in src/core:
- After the window's frames (`QQuickWindow::afterFrameEnd`, about once a second), the
  delegate item passes the frame time and the screen's refresh interval to the view's
  `ui::Compositor::SetDisplayVSyncParameters`.
- Chromium's clock then runs at the screen's rate, in phase with Qt.

## Measurements

The method: on the phone, a throwaway browser instance with `QSG_RENDER_TIMING=1` loads
en.m.wikipedia.org/wiki/Norway. After 15 s, three touch swipes of 250 ms run through
mobarchy-mcp (uinput). The measure is the intervals between the Qt Quick window's
`syncAndRender` starts during the swipes. The scripts were /tmp/measure.sh and
/tmp/idleframes.sh on the phone.

Both engines are the same build otherwise (6.11.2, Arch Linux ARM), cp3a, Mesa Panfrost.

| | frames during the swipes | ≤ 10 ms (120 Hz) | 14–19 ms | > 19 ms |
|---|---|---|---|---|
| stock 6.11.2-1 (3 runs) | 190, 194, 194 | 14–17 % | 65–70 % | 6–8 % |
| patched 6.11.2-1.1 (3 runs) | 345, 297, 285 | 90 % | 1–5 % | 0–2 % |

Idle, 8 s, no input:

| | static page (example.com) | Wikipedia (it animates) | CPU, Wikipedia |
|---|---|---|---|
| stock | 0 frames | 405 frames (~50/s) | 36 % of a core |
| patched | 0 frames | 777 frames (~97/s) | 70 % of a core |

A static page draws nothing in either engine: the patch adds no frames of its own. An
animating page now animates at the screen's rate, as Chrome does on a 120 Hz phone, and
costs accordingly.

Ruled out: `--disable-gpu-vsync` (no change). `--disable-frame-rate-limit` gave ~95 % at
120 Hz but kept an idle page at 124 % of a core.

## Upstream

This is worth a report to Qt (bugreports.qt.io, component WebEngine), with the patch and
these numbers.
