"""mobarchy-mcp-call: a minimal MCP client for scripts and agents.

    mobarchy-mcp-call tools
    mobarchy-mcp-call call screenshot --image-out shot.png
    mobarchy-mcp-call call tap '{"x": 200, "y": 400}'
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import json
import sys

from mcp import Client

DEFAULT_URL = "http://127.0.0.1:8765/mcp"


async def run(args) -> int:
    async with Client(args.url) as client:
        if args.cmd == "tools":
            for t in (await client.list_tools()).tools:
                risk = (t.meta or {}).get("mobarchy/risk", "?")
                print(f"{t.name:14} [{risk:7}] {t.description.splitlines()[0]}")
            return 0
        result = await client.call_tool(args.name, json.loads(args.arguments))
        n_images = 0
        for c in result.content:
            if c.type == "text":
                print(c.text)
            elif c.type == "image":
                path = args.image_out if n_images == 0 else f"{args.image_out}.{n_images}"
                with open(path, "wb") as f:
                    f.write(base64.b64decode(c.data))
                print(f"[image {c.mime_type} saved to {path}]")
                n_images += 1
        return 1 if result.is_error else 0


def main() -> None:
    p = argparse.ArgumentParser(prog="mobarchy-mcp-call", description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--url", default=DEFAULT_URL)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("tools", help="list tools")
    c = sub.add_parser("call", help="call a tool")
    c.add_argument("name")
    c.add_argument("arguments", nargs="?", default="{}", help="JSON object")
    c.add_argument("--image-out", default="mcp-image.png")
    args = p.parse_args()
    try:
        sys.exit(asyncio.run(run(args)))
    except Exception as e:  # transport errors arrive wrapped in exception groups
        while isinstance(e, BaseExceptionGroup):
            e = e.exceptions[0]
        print(f"mobarchy-mcp-call: {args.url}: {type(e).__name__}: {e}", file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    main()
