# Agent layer

Mobarchy's core: the agent runtime and the OS tool layer.

| Component | Status |
|---|---|
| [`mobarchy-mcp/`](mobarchy-mcp/) | OS tool layer as an MCP server. v0.1 works on the simulator. |
| [`mobarchy-agent/`](mobarchy-agent/) | Agent runtime (Claude API). v0.1: CLI + JSON events for the on-device bar. Local LLM routing not started. |

See [ADR 0004](../docs/decisions/0004-compositor-agnostic-tool-api.md) for the tool API rules.
