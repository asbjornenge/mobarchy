"""The Claude Code backend's stream parsing and the agent state, without Claude Code."""

import json

from mobarchy_agent import agents


def line(obj):
    return json.dumps(obj)


def test_stream_assistant_text_and_tool():
    events, sid = agents.parse_claude_stream(line({
        "type": "assistant", "session_id": "s1",
        "message": {"content": [
            {"type": "text", "text": "Looking at the screen."},
            {"type": "tool_use", "name": "mcp__mobarchy__tap", "input": {"x": 10, "y": 20}},
        ]},
    }))
    assert sid == "s1"
    assert [(e.kind, e.text) for e in events] == [
        ("text", "Looking at the screen."),
        ("tool", 'tap {"x":10,"y":20}'),
    ]


def test_stream_result_success_and_error():
    events, _ = agents.parse_claude_stream(line({"type": "result", "subtype": "success",
                                                 "is_error": False, "result": "Done.", "session_id": "s1"}))
    assert [(e.kind, e.text) for e in events] == [("done", "Done.")]
    events, _ = agents.parse_claude_stream(line({"type": "result", "subtype": "success", "is_error": True,
                                                 "result": "Not logged in · Please run /login"}))
    assert events[0].kind == "error" and "login" in events[0].text
    events, _ = agents.parse_claude_stream(line({"type": "result", "subtype": "error_max_turns", "is_error": False}))
    assert events[0].kind == "error"


def test_stream_ignores_noise():
    assert agents.parse_claude_stream("not json") == ([], None)
    events, sid = agents.parse_claude_stream(line({"type": "system", "subtype": "init", "session_id": "s2"}))
    assert events == [] and sid == "s2"


def test_state_roundtrip(tmp_path, monkeypatch):
    monkeypatch.setattr(agents, "STATE_FILE", tmp_path / "state.json")
    assert agents.State.load().active is None
    st = agents.State(active="claude", conversation="abc")
    st.save()
    again = agents.State.load()
    assert again.active == "claude" and again.conversation == "abc"


def test_codex_stream():
    events, tid = agents.parse_codex_stream(line({"type": "thread.started", "thread_id": "t1"}))
    assert tid == "t1" and events == []
    events, _ = agents.parse_codex_stream(line({"type": "item.started", "item": {
        "id": "i1", "type": "mcp_tool_call", "server": "mobarchy", "tool": "screenshot", "arguments": {}}}))
    assert [(e.kind, e.text) for e in events] == [("tool", "screenshot")]
    events, _ = agents.parse_codex_stream(line({"type": "item.completed", "item": {
        "id": "i2", "type": "agent_message", "text": "Battery is at 100 %."}}))
    assert [(e.kind, e.text) for e in events] == [("text", "Battery is at 100 %.")]
    events, _ = agents.parse_codex_stream(line({"type": "turn.completed", "usage": {}}))
    assert [e.kind for e in events] == ["done"]
    events, _ = agents.parse_codex_stream(line({"type": "turn.failed", "error": {"message": "401 Unauthorized"}}))
    assert events[0].kind == "error" and "401" in events[0].text


def test_local_tool_schema_and_results():
    from types import SimpleNamespace as NS

    from mobarchy_agent import local
    tool = NS(name="tap", description="Tap.", input_schema={"type": "object", "properties": {"x": {"type": "number"}}})
    assert local._openai_tool(tool)["function"]["name"] == "tap"
    result = NS(is_error=False, content=[NS(type="text", text="ok"), NS(type="image")])
    assert local._result_text(result) == "ok\n[an image; you cannot see images]"
    assert local._result_text(NS(is_error=True, content=[NS(type="text", text="denied")])) == "ERROR: denied"


def test_task_url_names_a_new_task():
    from mobarchy_agent.agents import task_url
    a, b = task_url("http://127.0.0.1:8765/mcp"), task_url("http://127.0.0.1:8765/mcp")
    assert a.startswith("http://127.0.0.1:8765/mcp?task=") and a != b
    assert "&task=" in task_url("http://h/mcp?x=1")
