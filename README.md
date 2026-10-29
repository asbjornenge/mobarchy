# Mobarchy

An AI-first mobile Linux OS. The OS itself is exposed to AI agents as tools (MCP), so an agent can see and do everything a user can do on the phone.
Inspired by [Omarchy](https://omarchy.org).

Status: early development, in daily use on a Pixel 9 Pro Fold (`devices/comet`). Expect rough
edges, and a phone you are willing to experiment on.

| | | | |
|:-:|:-:|:-:|:-:|
| ![Boot](docs/screenshots/boot.png) | ![Lock screen](docs/screenshots/lock.png) | ![Home](docs/screenshots/home.png) | ![Notifications](docs/screenshots/notifications.png) |
| Boot | Lock screen | Home: widgets | Notifications |
| ![Apps](docs/screenshots/apps.png) | ![Quick settings](docs/screenshots/quick.png) | ![Weather forecast](docs/screenshots/forecast.png) | ![Themes](docs/screenshots/themes.png) |
| Apps | Quick settings | Weather | Themes |

(Screenshots from the simulator, with made-up data; the boot screen is the splash as the phone draws it.)

## Features

**AI-first**
- The whole phone as MCP tools (85 of them): the screen, apps, calls and messages, mail,
  calendar, contacts, settings, themes, widgets and more. Each tool has a risk level; risky
  steps and root are approved by the owner on the screen.
- **Frigg**, the phone's agent: talk to it ("Hei Frigg", a live voice conversation that
  knows the owner's voice) or type. It runs on the owner's own Claude or OpenAI account, a
  local model, or an API key.
- The agent makes things for the owner: themes (colours, fonts, wallpapers) and home screen
  widgets (news, prices, quotes, ...) as descriptions the shell draws, never as code.

**The shell** (Qt Quick on Hyprland)
- Home as pages: notifications, a board of widgets to arrange, and the apps.
- Lock screen with the owner's PIN (their password) and the fingerprint sensor.
- Its own on-screen keyboard (with emoji search), and hardware keyboards with shortcuts.
- Foldables: the screen follows the fold and turns with the device (a Hyprland plugin of
  its own), and a half-open phone becomes a laptop.

**Apps**
- Phone and messages, contacts and calendar (iCloud and Google), mail, clock with alarms and
  timers, camera and photos.
- Bifrost, the browser (with DRM video, 1Password and web apps on the home screen); Chromium
  for sites that need extensions.
- Spotify, a curated app store on top of Arch Linux, and Android apps through Waydroid
  (experimental).

**On the Pixel 9 Pro Fold**
- Arch Linux ARM on its own build of Google's kernel: the GPU (Panthor and Mesa), both
  displays and touchscreens, the fold sensor, rotation, GNSS, fingerprint, cameras, deep sleep.
- Calls, SMS and mobile data through the phone's own modem (the vendor's radio stack in a
  small Android container), and a Wi-Fi hotspot.

**For developers**
- A QEMU simulator that behaves as a device, driven through the same MCP tools.
- Device-specific code kept to `devices/<codename>/`: a new phone is a new directory.

## Layout

```
agent/     Agent runtime and the OS tool layer (MCP servers). This is Mobarchy's core.
os/        Generic userspace. Device-agnostic. config/ = session config, rootfs/ = system files,
           initramfs/ = debug init for first boots, kernel/ = kernel config fragment,
           image/ = device rootfs image builder (Arch Linux ARM).
devices/   One directory per device (boot, kernel, hardware adaptation). Kept thin.
sim/       The simulator: a QEMU/KVM VM that acts as a device, plus tools to drive it.
tools/     Containerized helpers: Android image unpacking, kernel builds.
docs/      Brief, architecture, and decision records (docs/decisions/).
```

The key rule: **nothing outside `devices/` may assume a specific phone.** See [devices/README.md](devices/README.md).

## Quick start (simulator)

Requirements: Linux host with KVM, `qemu-system-x86_64`, OVMF, `xorriso`, Python 3.11+.

```sh
sim/mobsim create      # download base image, build disk + cloud-init seed
sim/mobsim up          # boot, wait for the shell (first boot provisions, ~5 min), deploy the tree

# On the phone: tap "✦ Ask" in the bottom bar and type a task (needs an API key, see agent/mobarchy-agent).

# Drive the phone through its MCP server, like an agent would:
cd agent/mobarchy-mcp
uv run mobarchy-mcp-call tools
uv run mobarchy-mcp-call call screenshot --image-out shot.png
uv run mobarchy-mcp-call call tap '{"x": 225, "y": 500}'
```

See [sim/README.md](sim/README.md) for all commands.

## Documentation

- [Project brief](docs/brief.md)
- [Architecture](docs/architecture.md)
- [Decision records](docs/decisions/)
- [Research notes](docs/research/)

## License

Mobarchy is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, either version 3 of
the License, or (at your option) any later version ([GPL-3.0-or-later](LICENSE)).

Some parts keep the licenses of the projects they come from or change:
- the kernel patches (`devices/*/kernel/`): GPL-2.0, as the Linux kernel;
- the patches and build files for other projects' packages (`os/packages/`): those projects'
  licenses (the packages for proprietary software, 1Password and Widevine, contain only the
  build files that fetch it);
- the Caveat font (`os/shell/data/fonts/`): SIL Open Font License 1.1;
- the emoji data (`os/shell/data/emoji.json`): the Unicode License.
