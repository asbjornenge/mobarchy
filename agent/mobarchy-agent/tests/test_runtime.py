"""Run the agent loop against a scripted Messages API and a real in-process MCP server."""

import asyncio
import json

import anthropic
import httpx2
from mcp.server.mcpserver import Image, MCPServer

from mobarchy_agent.config import Config
from mobarchy_agent.runtime import run_task

PNG_1PX = bytes.fromhex(
    "89504e470d0a1a0a0000000d4948445200000001000000010806000000"
    "1f15c4890000000d49444154789c6360000002000154a24f5d0000000049454e44ae426082"
)


def make_phone():
    phone = MCPServer(name="fake-phone")
    taps = []

    @phone.tool()
    def screenshot() -> Image:
        """Capture the screen."""
        return Image(data=PNG_1PX, format="png")

    @phone.tool()
    def tap(x: float, y: float) -> str:
        """Tap."""
        taps.append((x, y))
        return "ok"

    return phone, taps


def message(content, stop_reason):
    return {
        "id": "msg_test", "type": "message", "role": "assistant", "model": "claude-opus-5",
        "content": content, "stop_reason": stop_reason, "stop_sequence": None,
        "usage": {"input_tokens": 10, "output_tokens": 10},
    }


def scripted_api(responses, requests):
    def handler(request: httpx2.Request) -> httpx2.Response:
        requests.append(json.loads(request.content))
        return httpx2.Response(200, json=responses[len(requests) - 1])

    return anthropic.AsyncAnthropic(
        api_key="test", http_client=httpx2.AsyncClient(transport=httpx2.MockTransport(handler))
    )


def test_agent_loop_runs_tools_and_reports():
    phone, taps = make_phone()
    requests, events = [], []
    client = scripted_api([
        message([{"type": "tool_use", "id": "tu_1", "name": "screenshot", "input": {}}], "tool_use"),
        message([{"type": "tool_use", "id": "tu_2", "name": "tap", "input": {"x": 10, "y": 20}}], "tool_use"),
        message([{"type": "text", "text": "Tapped the button."}], "end_turn"),
    ], requests)

    result = asyncio.run(run_task("tap the button", Config(), events.append, client=client, mcp_target=phone))

    assert result == "Tapped the button."
    assert taps == [(10, 20)]
    assert [e.kind for e in events] == ["tool", "tool", "text", "done"]
    # The screenshot went back to Claude as an image tool result.
    tool_result = requests[1]["messages"][-1]["content"][0]
    assert tool_result["type"] == "tool_result"
    assert tool_result["content"][0]["type"] == "image"
    # The request carries the phone's tools and the agent settings.
    assert {t["name"] for t in requests[0]["tools"]} == {"screenshot", "tap"}
    assert requests[0]["thinking"] == {"type": "adaptive"}


def test_refusal_is_reported_as_error():
    phone, _ = make_phone()
    events = []
    client = scripted_api([message([], "refusal")], [])
    asyncio.run(run_task("do something bad", Config(), events.append, client=client, mcp_target=phone))
    assert events[-1].kind == "error"
