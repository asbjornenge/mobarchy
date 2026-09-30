"""mobarchy-agent: give the phone a task from the command line.

    mobarchy-agent "open a terminal and show the uptime"
"""

from __future__ import annotations

import argparse
import asyncio
import json
import sys

from .config import Config
from .runtime import Event, run_task

PREFIX = {"text": "💬", "tool": "  →", "done": "✓", "error": "✗"}


def main() -> None:
    p = argparse.ArgumentParser(prog="mobarchy-agent", description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("task")
    p.add_argument("--url", help="MCP server URL")
    p.add_argument("--model")
    p.add_argument("--effort", choices=["low", "medium", "high", "xhigh", "max"])
    p.add_argument("--json", action="store_true", help="print events as JSON lines (for UIs)")
    args = p.parse_args()

    config = Config.load()
    if args.url:
        config.mcp_url = args.url
    if args.model:
        config.model = args.model
    if args.effort:
        config.effort = args.effort

    failed = False

    def show(e: Event) -> None:
        nonlocal failed
        failed |= e.kind == "error"
        if args.json:
            print(json.dumps({"kind": e.kind, "text": e.text}, ensure_ascii=False), flush=True)
        elif e.kind != "done":
            print(f"{PREFIX[e.kind]} {e.text}", flush=True)

    asyncio.run(run_task(args.task, config, show))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
