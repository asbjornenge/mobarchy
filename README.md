# Mobarchy

An AI-first mobile Linux OS. The OS itself is exposed to AI agents as tools (MCP), so an agent can see and do everything a user can do on the phone.
Inspired by [Omarchy](https://omarchy.org).

Status: early development. Nothing boots on real hardware yet.

## Layout

```
agent/     Agent runtime and the OS tool layer (MCP servers). This is Mobarchy's core.
os/        Generic userspace. Device-agnostic. config/ = session config, rootfs/ = system files.
devices/   One directory per device (boot, kernel, hardware adaptation). Kept thin.
sim/       The simulator: a QEMU/KVM VM that acts as a device, plus tools to drive it.
docs/      Brief, architecture, and decision records (docs/decisions/).
```

The key rule: **nothing outside `devices/` may assume a specific phone.** See [devices/README.md](devices/README.md).

## Quick start (simulator)

Requirements: Linux host with KVM, `qemu-system-x86_64`, OVMF, `xorriso`, Python 3.11+.

```sh
sim/mobsim create      # download base image, build disk + cloud-init seed
sim/mobsim up          # boot, wait for the shell (first boot provisions, ~5 min), deploy the tree

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
