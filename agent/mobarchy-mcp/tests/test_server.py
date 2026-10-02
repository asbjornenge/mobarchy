"""In-process tests of the MCP surface with a fake compositor and touchscreen."""

import asyncio
import json

import pytest
from mcp import Client

from mobarchy_mcp import server
from mobarchy_mcp.compositor import Compositor, Output, Window
from mobarchy_mcp.policy import Decision, Policy, Risk


class FakeCompositor(Compositor):
    name = "fake"

    def __init__(self):
        self.launched: list[str] = []

    def outputs(self):
        return [Output(name="FAKE-1", width=1080, height=2424, scale=2.4)]

    def windows(self):
        return [Window("0x1", "foot", "term", True, 0, 0, 450, 1010, "FAKE-1")]

    def focus(self, window_id):
        pass

    def close(self, window_id):
        pass

    def launch(self, command):
        self.launched.append(command)


class FakeTouch:
    def __init__(self):
        self.taps: list[tuple[float, float]] = []

    def tap(self, fx, fy, hold):
        self.taps.append((fx, fy))


@pytest.fixture
def fakes(monkeypatch):
    c, t = FakeCompositor(), FakeTouch()
    monkeypatch.setattr(server, "_compositor", c)
    monkeypatch.setattr(server, "_touch", t)
    monkeypatch.setattr(server, "policy", Policy())
    return c, t


def call(name, args=None):
    async def go():
        async with Client(server.mcp) as client:
            return await client.call_tool(name, args or {})
    return asyncio.run(go())


def list_tools():
    async def go():
        async with Client(server.mcp) as client:
            return (await client.list_tools()).tools
    return asyncio.run(go())


def test_every_tool_declares_a_risk_level():
    tools = list_tools()
    assert tools
    for t in tools:
        assert (t.meta or {}).get("mobarchy/risk") in {r.value for r in Risk}, t.name


def test_tap_maps_logical_pixels_to_fractions(fakes):
    _, touch = fakes
    r = call("tap", {"x": 225, "y": 505})
    assert not r.is_error
    assert touch.taps == [(0.5, 0.5)]


def test_rotated_output_swaps_logical_size():
    # comet's inner panel: 2152x2076 scanned out, turned by 270 degrees on screen.
    out = Output(name="DSI-1", width=2152, height=2076, scale=2, transform=3)
    assert (out.logical_width, out.logical_height) == (1038, 1076)
    assert (Output("X", 2152, 2076, 2, transform=2).logical_width) == 1076


def test_tap_outside_screen_is_an_error(fakes):
    r = call("tap", {"x": 451, "y": 10})
    assert r.is_error


def test_confirm_tools_denied_by_default(fakes):
    r = call("run_shell", {"command": "true"})
    assert r.is_error
    assert "denied by device policy" in r.content[0].text


def test_policy_override_allows_confirm_tool(fakes, monkeypatch):
    monkeypatch.setattr(server, "policy", Policy(tools={"run_shell": Decision.ALLOW}))
    r = call("run_shell", {"command": "echo hi"})
    assert not r.is_error
    assert json.loads(r.content[0].text)["stdout"] == "hi\n"


def test_list_windows(fakes):
    r = call("list_windows")
    assert json.loads(r.content[0].text)["windows"][0]["app_id"] == "foot"
