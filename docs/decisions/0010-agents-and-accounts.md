# 0010: Agents with the owner's own accounts (Claude, OpenAI) and a local fallback

- Status: accepted
- Date: 2026-10-02

## Context

The agent so far called the Anthropic API with an API key (ADR 0006 era). The owner wants
the phone's agent to run on their own accounts: their Claude subscription (cost, the MCP
servers and memory tied to the account) and their OpenAI/ChatGPT account. It must also
work without a network.

Anthropic's Agent SDK documentation says: "Unless previously approved, Anthropic does not
allow third party developers to offer claude.ai login or rate limits for their products,
including agents built on the Claude Agent SDK." Mobarchy does not offer a claude.ai login
of its own. It runs the owner's own Claude Code, installed from Anthropic and logged in
with Claude Code's own login, the way the owner's other tooling (chattr) already does.
Whether a *distributed* Mobarchy may do this is open; see Consequences.

## Decision

`mobarchy-agent` gets several backends behind the same JSON event stream (ADR 0006). The
shell offers a choice: "Choose your agent".

| Agent | Runs | Account / login |
|---|---|---|
| `claude` | the owner's Claude Code, `claude -p --output-format stream-json`, with the phone's MCP server as its only tools | Claude Code's own (`claude auth login`: subscription or Console). Installed on demand with Anthropic's installer, never shipped in the image |
| `openai` | the owner's Codex (`codex exec --json`) with the phone's MCP server | Codex's own (`codex login`: ChatGPT account or API key) |
| `local` | a small model on the phone (llama.cpp), with the tool loop in mobarchy-agent | none; always there; the fallback without network |
| `api` | the Anthropic API directly (the existing runtime) | an API key in `~/.config/mobarchy/agent.env` |

- Logins belong to the tools (`~/.claude`, `~/.codex`). Mobarchy never handles tokens.
  Several agents can be logged in at once; one is active, and the owner switches in the
  home screen.
- Login on the phone: the tool opens the browser (Firefox) on its OAuth page with a
  localhost callback, so the login completes on the phone without copying codes.
- Only the phone's tools: `--strict-mcp-config` with mobarchy-mcp, and only
  `mcp__mobarchy__*` allowed. The agent has no shell or file tools of its own; what it may
  do on the phone is what mobarchy-mcp and its risk policy (ADR 0004) allow.
- The home screen is one conversation: the agent's session is resumed for follow-ups.
- CLI: `mobarchy-agent agents --json`, `mobarchy-agent use <agent>`,
  `mobarchy-agent setup <agent> --json`, `mobarchy-agent --json [--agent X] [--resume ID] TASK`.

## Consequences

- For the owner's phone this works now. Before Mobarchy is distributed, ask Anthropic (and
  check OpenAI's terms) whether an OS may run the user's own Claude Code / Codex this way.
  If not, a distributed build offers `api` (and `local`) only.
- Claude Code and Codex are proprietary and installed per user from their vendors.
- The local agent needs a model download and is slow on CPU; it is the fallback, not the default.
