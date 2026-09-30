# mobarchy-agent

The agent runtime. Claude operates the phone through the phone's own MCP tool layer ([mobarchy-mcp](../mobarchy-mcp/)).

- It discovers the tools from the MCP server at runtime, so new tools become available to the agent automatically.
- The loop is the Anthropic SDK tool runner, with MCP tools converted by `anthropic.lib.tools.mcp`.
- Model: `claude-opus-5` with adaptive thinking, effort `medium` by default (a latency tradeoff for a phone UI).
- Old tool results (mostly screenshots) are cleared automatically as the context grows (context editing), and server-side refusal fallbacks are enabled.
- The permission policy is enforced by the tool layer, not here.

## Configuration

Settings come from environment variables, which can be supplied by an env file. The first of these that exists is used:

1. `$MOBARCHY_AGENT_ENV`
2. `~/.config/mobarchy/agent.env`
3. `/etc/mobarchy/agent.env`

```sh
ANTHROPIC_API_KEY=sk-ant-...        # required (or an `ant auth login` profile)
MOBARCHY_AGENT_MODEL=claude-opus-5  # optional
MOBARCHY_AGENT_EFFORT=medium        # low | medium | high | xhigh | max
MOBARCHY_MCP_URL=http://127.0.0.1:8765/mcp
```

**Never commit this file.** `sim/mobsim sync` copies the host's `~/.config/mobarchy/agent.env` into the simulator (mode 600) if it exists.

## Usage

```sh
uv run mobarchy-agent "open a terminal and show the uptime"
uv run mobarchy-agent --json "..."   # JSON-lines events, used by mobarchy-bar
```

Events: `tool` (a tool call), `text` (the agent speaking), `done` (final message), `error`.

## Tests

```sh
uv run pytest
```

The tests run the real loop against a scripted Messages API (an httpx mock transport) and an in-process MCP server, so they need no API key.
