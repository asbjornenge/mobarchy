# Simulator

`mobsim` runs the device `devices/sim-x86_64` as a QEMU/KVM VM: an Arch Linux guest with Hyprland on a portrait 1080x2424 display, plus a virtio multitouch screen.

It is designed for agents. Commands are non-interactive and exit non-zero on failure. Observation and input go through QEMU (QMP) from the host, so they work even when the guest is broken, much like a webcam and a robot finger on a real phone.

State lives outside the repo in `~/.cache/mobarchy/sim` (override with `MOBARCHY_SIM_DIR`).

## Commands

| Command | What it does |
|---|---|
| `create [--force]` | Download the Arch cloud image, create an overlay disk and a cloud-init seed. |
| `start [--gui]` | Boot the VM (headless by default). |
| `stop [--force]` | ACPI shutdown (or kill). |
| `status` | JSON: running, ssh reachable, shell up. |
| `wait [--timeout N]` | Block until provisioned and Hyprland is running. |
| `log [-n N]` | Tail the serial console. |
| `sync` | Deploy the working tree: `os/rootfs` + device overlay, `os/config`, `device.toml`, and `agent/` (then `uv sync` and a restart of `mobarchy-mcp`). |
| `up [--gui]` | `start` + `wait` + `sync`. This is the normal way to get a fresh, up-to-date simulator. |
| `screenshot [file.png]` | Capture the display from the host side. |
| `tap X Y [--hold ms]` | Touch at display pixel coordinates. |
| `swipe X1 Y1 X2 Y2 [--duration ms]` | Single-finger swipe. |
| `key COMBO` | Press keys by QEMU qcode, e.g. `ret`, `meta_l-ret`, `ctrl-c`. |
| `type TEXT` | Type text into the focused window (wtype in the guest). |
| `hyprctl ARGS...` | Run hyprctl in the guest session, e.g. `hyprctl -j clients`. |
| `ssh [CMD...]` | Shell into the guest as user `mob` (passwordless sudo). |

## Notes

- Agent credentials: if `~/.config/mobarchy/agent.env` exists on the host (outside the repo), `sync` copies it into the guest (mode 600). See `agent/mobarchy-agent/README.md`.
- Host ports: ssh on `127.0.0.1:2222`, MCP (`mobarchy-mcp`) on `http://127.0.0.1:8765/mcp`.
- `mobsim tap/swipe` coordinates are physical display pixels (1080x2424). Hyprland runs at scale 2.4, so the logical size is 450x1010. (The MCP tools use logical pixels.)
- Touch injection uses QMP `input-send-event` with multitouch (`mtt`) events. Each contact gets a fresh tracking id, and release sends tracking id -1. Do not pass `device=` to `input-send-event`: it names a display console, and passing the touch device crashes QEMU.
- The guest renders in software (llvmpipe, no virgl), so it does not depend on the host GPU.
- Provisioning happens on first boot through cloud-init (`user_data()` in `mobsim`). To change what gets installed, edit it and run `create --force`.
