# Mobarchy: notes for agents

Read `README.md`, `docs/architecture.md` and `docs/decisions/` before making structural changes. The original brief is in `docs/brief.md`.

## Hard rules

- **Never** do anything that can brick a phone or wipe data (flashing, unlocking, erasing or writing partitions) without explicit sign-off from Asbjørn in the current conversation.
- Only `devices/<codename>/` may contain device-specific code. Generic code reads `device.toml`. See `devices/README.md`.
- Never commit secrets. API keys live in `~/.config/mobarchy/agent.env` (outside the repo). The repo is headed for public hosting.
- Do not run git commands that modify the repository (commit, push, branch, reset...) unless Asbjørn asks.
- Record significant decisions as ADRs in `docs/decisions/` (see 0001).

## Testing

- Unit tests: `uv run pytest` in `agent/mobarchy-mcp` and in `agent/mobarchy-agent`. They need no API key and no simulator.
- On the simulator: `sim/mobsim up` deploys the working tree (run `sim/mobsim sync` after later edits). Then drive the device through MCP with `uv run mobarchy-mcp-call ...` (in `agent/mobarchy-mcp`), or at a lower level with `sim/mobsim screenshot/tap/type/hyprctl/ssh`.
- Debug initramfs (device boot path): `os/initramfs/test-sim` boots it in QEMU (debug and switch_root scenarios). It needs the simulator running.
- Look at a screenshot before you claim that a UI change works.

The dev host's desktop also runs Hyprland. Never run `hyprctl` or Wayland tools against the host session; go through `sim/mobsim`.
