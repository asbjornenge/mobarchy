"""The agent loop: Claude operating the phone through the phone's own MCP tool layer.

The tools come from mobarchy-mcp at runtime, so the agent gains new abilities as
soon as the tool layer does. The permission policy is enforced by the tool layer,
not here.
"""

from __future__ import annotations

import json
from collections.abc import Awaitable, Callable
from dataclasses import dataclass

import anthropic
from anthropic.lib.tools.mcp import async_mcp_tool
from mcp import Client

from .config import Config

SYSTEM_PROMPT = """\
You are the assistant built into a Mobarchy phone, a Linux phone whose operating system is \
exposed to you as tools. You act on the owner's behalf by operating the phone the way a person would.

How to work:
- Start by taking a screenshot to see the current screen. Coordinates for tap and swipe are \
logical pixels in the same space as the screenshot image.
- Prefer the most direct tool: launch_app over hunting for icons, type_text over tapping keys.
- After each meaningful action, take a screenshot to confirm it worked before moving on.
- The Mobarchy home screen (the chat with you) is your own interface. Don't tap it or type into it.
- Some tools are marked risk level "confirm" (irreversible or outward-facing). If one is denied \
by the device policy, don't look for workarounds; tell the owner what you would have done and why \
it needs their permission.

When you are done, reply with one or two short sentences for the owner: what you did, or what \
stopped you. The owner reads this on a small screen."""

# Screenshots pile up quickly; clear old tool results once the context grows.
CONTEXT_MANAGEMENT = {"edits": [{"type": "clear_tool_uses_20250919"}]}
BETAS = ["context-management-2025-06-27", "server-side-fallback-2026-07-01"]


@dataclass
class Event:
    kind: str  # "text" | "tool" | "done" | "error" | "url" (a link for the owner to open)
    text: str


EventHandler = Callable[[Event], Awaitable[None] | None]


async def _emit(handler: EventHandler | None, event: Event) -> None:
    if handler is None:
        return
    r = handler(event)
    if r is not None:
        await r


def _describe_tool_call(name: str, args: dict) -> str:
    if not args:
        return name
    return f"{name} {json.dumps(args, ensure_ascii=False, separators=(',', ':'))}"


async def run_task(
    prompt: str,
    config: Config | None = None,
    on_event: EventHandler | None = None,
    *,
    client: anthropic.AsyncAnthropic | None = None,
    mcp_target: object | None = None,
) -> str:
    """Carry out one task on the phone. Returns the agent's final message for the owner.

    `client` and `mcp_target` (a URL or an in-process MCP server) are injectable for tests.
    """
    config = config or Config.load()
    if client is None:
        if not config.has_credentials():
            msg = "No Anthropic credentials. Put ANTHROPIC_API_KEY in ~/.config/mobarchy/agent.env."
            await _emit(on_event, Event("error", msg))
            return msg
        client = anthropic.AsyncAnthropic()

    async with Client(mcp_target or config.mcp_url) as mcp:
        listed = await mcp.list_tools()
        tools = [async_mcp_tool(t, mcp.session) for t in listed.tools]

        runner = client.beta.messages.tool_runner(
            model=config.model,
            max_tokens=16000,
            max_iterations=config.max_iterations,
            system=SYSTEM_PROMPT,
            thinking={"type": "adaptive"},
            output_config={"effort": config.effort},
            cache_control={"type": "ephemeral"},
            context_management=CONTEXT_MANAGEMENT,
            fallbacks="default",
            betas=BETAS,
            tools=tools,
            messages=[{"role": "user", "content": prompt}],
        )

        final_text = ""
        last = None
        try:
            async for message in runner:
                last = message
                for block in message.content:
                    if block.type == "text" and block.text.strip():
                        final_text = block.text.strip()
                        await _emit(on_event, Event("text", final_text))
                    elif block.type == "tool_use":
                        await _emit(on_event, Event("tool", _describe_tool_call(block.name, block.input)))
        except anthropic.AuthenticationError:
            msg = "No valid Anthropic credentials (set ANTHROPIC_API_KEY in ~/.config/mobarchy/agent.env)."
            await _emit(on_event, Event("error", msg))
            return msg
        except anthropic.RateLimitError:
            msg = "The Claude API is rate limiting us. Try again in a moment."
            await _emit(on_event, Event("error", msg))
            return msg
        except anthropic.APIStatusError as e:
            msg = f"Claude API error {e.status_code}: {e.message}"
            await _emit(on_event, Event("error", msg))
            return msg
        except anthropic.APIConnectionError:
            msg = "Cannot reach the Claude API (is the phone online?)."
            await _emit(on_event, Event("error", msg))
            return msg

        if last is not None and last.stop_reason == "refusal":
            final_text = "I can't help with that request."
            await _emit(on_event, Event("error", final_text))
        elif last is not None and last.stop_reason == "tool_use":
            final_text = f"Stopped after {config.max_iterations} steps without finishing."
            await _emit(on_event, Event("error", final_text))
        else:
            await _emit(on_event, Event("done", final_text))
        return final_text
