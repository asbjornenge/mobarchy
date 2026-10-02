"""The local agent (ADR 0010): a small model on the phone, through llama.cpp's server
(OpenAI-compatible chat completions with tool calls), driving the phone's MCP tools.

It works without a network and is the fallback when the cloud agents can't be reached. The
model sees text only: tool results that are images are replaced by a note.
"""

from __future__ import annotations

import asyncio
import json
import os
import shutil
from pathlib import Path

import httpx2
from mcp import Client

from .config import Config
from .runtime import Event, EventHandler, _describe_tool_call, _emit

MODELS = Path.home() / ".local/share/mobarchy/models"
# Q4_0: llama.cpp repacks it for the ARM int8 matrix instructions (i8mm), which reads
# prompts ~1.7x faster than Q4_K_M on comet (54 vs 32 tokens/s).
MODEL_FILE = MODELS / "qwen3-4b-instruct-q4_0.gguf"
MODEL_URL = ("https://huggingface.co/unsloth/Qwen3-4B-Instruct-2507-GGUF/resolve/main/"
             "Qwen3-4B-Instruct-2507-Q4_0.gguf")
MODEL_BYTES = 2_375_773_280
MODEL_NAME = "Qwen3 4B"
SERVER_URL = os.environ.get("MOBARCHY_LLM_URL", "http://127.0.0.1:8766")
MAX_STEPS = 12

LOCAL_PROMPT = """\
You are the assistant built into a Mobarchy phone, running locally on the phone itself. \
The phone's operating system is available to you as tools. Use them to do what the owner \
asks. You cannot see images: prefer tools that return text (list_windows, list_apps, \
wifi_status, run_shell for reading state) over screenshots. Keep answers to one or two \
short sentences; the owner reads them on a small screen."""


def server_bin() -> str | None:
    return (shutil.which("llama-server") or
            next((str(p) for p in (Path("/opt/mobarchy/llama/bin/llama-server"),) if p.exists()), None))


def installed() -> bool:
    return server_bin() is not None and MODEL_FILE.exists()


async def _server_up(client: httpx2.AsyncClient) -> bool:
    try:
        r = await client.get(f"{SERVER_URL}/health", timeout=2)
        return r.status_code == 200
    except httpx2.HTTPError:
        return False


async def ensure_server(on_event: EventHandler) -> bool:
    """Start the model server (a user service) if it isn't running, and wait until the
    model is loaded."""
    async with httpx2.AsyncClient() as client:
        if await _server_up(client):
            return True
        await _emit(on_event, Event("tool", f"starting the local model ({MODEL_NAME})"))
        proc = await asyncio.create_subprocess_exec("systemctl", "--user", "start", "mobarchy-llm.service")
        await proc.wait()
        for _ in range(120):
            if await _server_up(client):
                return True
            await asyncio.sleep(0.5)
    await _emit(on_event, Event("error", "The local model did not start (mobarchy-llm.service)."))
    return False


def _openai_tool(tool) -> dict:
    return {"type": "function", "function": {
        "name": tool.name,
        "description": (tool.description or "")[:1000],
        "parameters": tool.input_schema or {"type": "object", "properties": {}},
    }}


def _result_text(result) -> str:
    parts = []
    for c in result.content:
        if getattr(c, "type", "") == "text":
            parts.append(c.text)
        elif getattr(c, "type", "") == "image":
            parts.append("[an image; you cannot see images]")
    text = "\n".join(parts) or "(no output)"
    return ("ERROR: " if result.is_error else "") + text[:6000]


async def run(prompt: str, config: Config, on_event: EventHandler, history: list[dict] | None = None) -> list[dict]:
    """One task. Returns the conversation, for a follow-up."""
    if not installed():
        await _emit(on_event, Event("error", "The local agent is not set up. Choose Local agent to set it up."))
        return history or []
    if not await ensure_server(on_event):
        return history or []
    messages = list(history or [{"role": "system", "content": LOCAL_PROMPT}])
    messages.append({"role": "user", "content": prompt})
    async with Client(config.mcp_url) as mcp, httpx2.AsyncClient(timeout=300) as http:
        tools = [_openai_tool(t) for t in (await mcp.list_tools()).tools]
        for _ in range(MAX_STEPS):
            r = await http.post(f"{SERVER_URL}/v1/chat/completions", json={
                "messages": messages, "tools": tools, "tool_choice": "auto", "temperature": 0.3,
            })
            if r.status_code != 200:
                await _emit(on_event, Event("error", f"Local model error {r.status_code}: {r.text[:200]}"))
                return messages
            msg = r.json()["choices"][0]["message"]
            messages.append({k: v for k, v in msg.items() if k in ("role", "content", "tool_calls")})
            calls = msg.get("tool_calls") or []
            text = (msg.get("content") or "").strip()
            if not calls:
                await _emit(on_event, Event("done", text))
                return messages
            if text:
                await _emit(on_event, Event("text", text))
            for call in calls:
                name = call["function"]["name"]
                try:
                    args = json.loads(call["function"].get("arguments") or "{}")
                except ValueError:
                    args = {}
                await _emit(on_event, Event("tool", _describe_tool_call(name, args)))
                try:
                    content = _result_text(await mcp.call_tool(name, args))
                except Exception as e:  # a bad call should go back to the model, not end the task
                    content = f"ERROR: {e}"
                messages.append({"role": "tool", "tool_call_id": call.get("id", ""), "content": content})
        await _emit(on_event, Event("error", f"Stopped after {MAX_STEPS} steps without finishing."))
        return messages


async def setup(on_event: EventHandler) -> bool:
    if not server_bin():
        await _emit(on_event, Event("error", "llama.cpp (llama-server) is not installed on this phone."))
        return False
    if MODEL_FILE.exists():
        return True
    MODELS.mkdir(parents=True, exist_ok=True)
    await _emit(on_event, Event("text", f"Downloading the local model ({MODEL_NAME}, 2.5 GB) …"))
    part = MODEL_FILE.with_suffix(".part")
    proc = await asyncio.create_subprocess_exec(
        "curl", "-fsSL", "-C", "-", "-o", str(part), MODEL_URL,
        stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.PIPE)
    total = MODEL_BYTES
    while proc.returncode is None:
        try:
            await asyncio.wait_for(proc.wait(), 10)
        except TimeoutError:
            if part.exists():
                await _emit(on_event, Event("tool", f"{part.stat().st_size * 100 // total} %"))
    if proc.returncode != 0:
        await _emit(on_event, Event("error", "Downloading the model failed (is the phone online?)."))
        return False
    part.rename(MODEL_FILE)
    return True
