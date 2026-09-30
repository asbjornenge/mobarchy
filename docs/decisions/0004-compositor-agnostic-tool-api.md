# 0004: The tool API is compositor-agnostic and carries risk levels

- Status: accepted
- Date: 2026-09-30

## Context

The OS tool layer (MCP) is Mobarchy's core product. Existing MCP servers (HyprMCP, hypruse) expose Hyprland directly. The compositor may change (see 0003), and agents will eventually act on high-stakes surfaces: calls, messages, and payments.

## Decision

1. Tool schemas describe phone concepts: apps, windows, screens, touch, text, notifications, and settings. Hyprland-specific tools (for example, raw `hyprctl`) may exist for development, but they are not part of the stable API.
2. Every tool declares a risk level:
   - `read`: observe only.
   - `act`: changes local state and is reversible.
   - `confirm`: irreversible or outward-facing, such as calls, messages, payments, or deleting data. Requires user confirmation under the default policy.
3. The permission policy is enforced in the tool layer, not trusted to the agent.

## Consequences

The first tool API design (Track B) must define the risk level for each tool and the enforcement point before any tool is implemented.
