# Mobarchy — Project Brief

> Hand-off brief for the development agent starting work on Mobarchy.
> Owner: Asbjørn Enge. Status: day 0, no code yet.

## What Mobarchy is

Mobarchy is an **AI-first mobile Linux OS**, a mobile counterpart to [Omarchy](https://omarchy.org) (DHH's opinionated, agent-friendly Arch + Hyprland desktop). The core idea: **the OS itself is exposed as tools for AI agents.** Everything a user can do on the phone, an agent should be able to see and do through a well-defined tool interface (MCP).

## Principles and constraints

- **AI-first.** The system is designed around agents, not retrofitted with them. System state, apps, windows, input, sensors, telephony, and settings are exposed as agent tools.
- **Hybrid inference.** Roughly 90% of inference goes to cloud providers (Anthropic first, others pluggable). **Local inference is required** so the phone stays useful offline. Target hardware must be able to run a local model.
- **General mobile OS.** Not foldable-only, but must handle foldables well (two screens, fold state, posture changes).
- **Mainline Linux is not required.** Using Android vendor kernels and blobs via Halium/libhybris is acceptable and expected.
- **Portable by design.** Keep the device-specific boot/hardware layer thin and swappable, so new phones can be added as they ship or get bootloader-unlocked.
- **Not locked to Omarchy's choices.** Hyprland is the default starting point, but we build new components when that serves mobile better.
- **Agents do the work.** Development happens with multiple AI agents. The dev setup must let agents see and test the phone themselves.

## Target hardware

| Device | Codename | SoC | Status |
|---|---|---|---|
| Google Pixel 9 Pro Fold | `comet` | Tensor G4 (`zumapro`), 16 GB RAM, Mali-G715 | **In hand. Primary dev device now.** |
| Jolla Phone (2026) | — | — | Ordered, not delivered. Second target. |

Note: there is no mainline or postmarketOS support for Tensor G4. postmarketOS only has a close-to-mainline kernel for gs101 (Pixel 6 series). For `comet`, the Halium route is the realistic one.

## Architecture (initial sketch)

```
┌──────────────────────────────────────────────┐
│ Agent runtime  (cloud LLM + local LLM router) │  ← Mobarchy-specific
├──────────────────────────────────────────────┤
│ OS tool layer (MCP servers: shell, windows,   │  ← Mobarchy-specific
│ input, screen, telephony, sensors, settings)  │
├──────────────────────────────────────────────┤
│ Mobile shell / UX (Hyprland + touch, or new)  │  ← reuse, then build
├──────────────────────────────────────────────┤
│ Userspace rootfs (Arch/Debian-based)          │  ← reuse
├──────────────────────────────────────────────┤
│ Hardware adaptation: Halium + libhybris       │  ← reuse / contribute
├──────────────────────────────────────────────┤
│ Device layer: kernel, device tree, boot image │  ← per-device, thin
└──────────────────────────────────────────────┘
```

The unique value of Mobarchy is the **top two layers**. Everything below should reuse existing work and contribute fixes upstream.

## Phase 1 — get to a working dev loop

Two tracks run in parallel so low-level porting doesn't block the agent layer.

### Track A — Boot port on `comet`
1. Back up everything first: stock factory image, and a dump of critical partitions if feasible. Never do anything that risks a hard brick without explicit sign-off from Asbjørn.
2. Unlock the bootloader and flash LineageOS (official `comet` support, 23.2 / Android 16) as a known-good baseline.
3. Use the LineageOS device tree and kernel as the base for a **halium-boot** for `comet`.
4. **Milestone A1:** boot halium-boot and get a shell over telnet/RNDIS. No GUI needed.
5. **Milestone A2:** boot a rootfs (start from Droidian as a reference) with a working display and touch.

### Track B — Agent tool layer, prototyped on desktop
1. Set up Hyprland on a regular Linux machine/VM.
2. Evaluate existing MCP servers for Hyprland: **hypruse** (computer use on Wayland: windows, input, screenshots) and **HyprMCP** (hyprctl over MCP).
3. Design the Mobarchy tool API: which tools, schemas, permission model. Look at Android MCP projects (e.g. ghost-in-the-droid) for which phone-specific tools agents actually need.
4. **Milestone B1:** an agent can operate a Hyprland session end to end through MCP.

### Track C — Dev and test rig
- Emulator setup for fast iteration where possible.
- Webcam rig pointed at the physical phone, so agents can visually verify boot and UI state.
- Scripted flash, boot, and log capture over USB, so agents can run boot loops without a human.

## Useful references

- Droidian porting guide: https://docs.droidian.org/porting-guide
- LineageOS `comet`: https://wiki.lineageos.org/devices/comet/
- Halium: https://github.com/Halium
- hypruse: https://pypi.org/project/hypruse/
- hyprgrass (Hyprland touch gestures): https://mynixos.com/nixpkgs/package/hyprlandPlugins.hyprgrass
- ghost-in-the-droid (Android MCP, 35 tools): https://pypi.org/project/ghost-in-the-droid/
- Omarchy: https://omarchy.org

## Landscape (as of Sept 2026)

- No known open project is building an agent-first mobile Linux OS. The niche is open.
- No Halium/Droidian port for `comet` was found. We may be first, and should upstream it.
- Omarchy has no mobile variant. The Omacom Foundation (~$10M) backs Omarchy. We plan to contact them **after** we have a demo.

## Open questions

- Base userspace: Arch (closer to Omarchy) or Debian (closer to Droidian tooling)?
- Hyprland + touch plugins vs. a purpose-built mobile compositor/shell?
- Local model runtime on Tensor G4 under Halium: CPU-only llama.cpp first, then GPU (Vulkan on Mali) and possibly the TPU?
- Permission and safety model for agents acting on the OS (calls, messages, payments).

## Working conventions

- Stop and ask before any action that can brick the device or wipe data.
- Keep device-specific code isolated from the generic OS layers.
- Log decisions briefly in `docs/decisions/` so other agents can pick up context.
