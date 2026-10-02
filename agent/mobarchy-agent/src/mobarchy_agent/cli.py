"""mobarchy-agent: give the phone a task, and manage which agent does it (ADR 0010).

    mobarchy-agent [--json] [--agent AGENT] [--new] "open a terminal and show the uptime"
    mobarchy-agent agents [--json]          the agents, and which ones are ready
    mobarchy-agent use AGENT                make AGENT the active one
    mobarchy-agent setup AGENT [--json]     install / log in (opens the browser)

Agents: claude (your Claude Code and Claude account), openai, local, api (API key).
"""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
from dataclasses import asdict

from . import agents
from .config import Config
from .runtime import Event

PREFIX = {"text": "💬", "tool": "  →", "done": "✓", "error": "✗", "url": "🔗"}


def _printer(as_json: bool):
    failed = False

    def show(e: Event) -> None:
        nonlocal failed
        failed |= e.kind == "error"
        if as_json:
            print(json.dumps({"kind": e.kind, "text": e.text}, ensure_ascii=False), flush=True)
        elif e.kind != "done" or e.text:
            print(f"{PREFIX.get(e.kind, '')} {e.text}", flush=True)

    return show, lambda: failed


def cmd_agents(argv: list[str]) -> int:
    p = argparse.ArgumentParser(prog="mobarchy-agent agents")
    p.add_argument("--json", action="store_true")
    args = p.parse_args(argv)
    statuses = asyncio.run(agents.statuses(Config.load()))
    active = agents.State.load().active
    if args.json:
        print(json.dumps({"active": active, "agents": [asdict(s) for s in statuses]}, ensure_ascii=False))
    else:
        for s in statuses:
            mark = "*" if s.id == active else " "
            state = "ready" if s.ready else ("installed" if s.installed else "-")
            print(f"{mark} {s.id:7} {state:9} {s.detail}")
    return 0


def cmd_use(argv: list[str]) -> int:
    p = argparse.ArgumentParser(prog="mobarchy-agent use")
    p.add_argument("agent", choices=["claude", "openai", "local", "api"])
    args = p.parse_args(argv)
    st = agents.State.load()
    st.active = args.agent
    st.save()
    return 0


def cmd_setup(argv: list[str]) -> int:
    p = argparse.ArgumentParser(prog="mobarchy-agent setup")
    p.add_argument("agent", choices=["claude", "openai", "local", "api"])
    p.add_argument("--json", action="store_true")
    args = p.parse_args(argv)
    show, failed = _printer(args.json)
    ok = asyncio.run(agents.setup(args.agent, show))
    return 0 if ok and not failed() else 1


def cmd_task(argv: list[str]) -> int:
    p = argparse.ArgumentParser(prog="mobarchy-agent", description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("task")
    p.add_argument("--agent", choices=["claude", "openai", "local", "api"])
    p.add_argument("--new", action="store_true", help="start a new conversation")
    p.add_argument("--url", help="MCP server URL")
    p.add_argument("--model", help="model (api agent)")
    p.add_argument("--effort", choices=["low", "medium", "high", "xhigh", "max"])
    p.add_argument("--json", action="store_true", help="print events as JSON lines (for UIs)")
    args = p.parse_args(argv)

    config = Config.load()
    if args.url:
        config.mcp_url = args.url
    if args.model:
        config.model = args.model
    if args.effort:
        config.effort = args.effort
    show, failed = _printer(args.json)
    asyncio.run(agents.run(args.task, config, show, agent=args.agent, new_conversation=args.new))
    return 1 if failed() else 0


def main() -> None:
    argv = sys.argv[1:]
    commands = {"agents": cmd_agents, "use": cmd_use, "setup": cmd_setup}
    if argv and argv[0] in commands:
        sys.exit(commands[argv[0]](argv[1:]))
    sys.exit(cmd_task(argv))


if __name__ == "__main__":
    main()
