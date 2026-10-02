"""The agents the owner can choose between (ADR 0010), behind one event stream.

    claude   the owner's own Claude Code (their Claude account), `claude -p`
    openai   the owner's own Codex (their OpenAI account), `codex exec --json`
    local    a small model on the phone, the offline fallback        [not yet]
    api      the Anthropic API with a key (runtime.py)

Logins belong to the tools (~/.claude, ~/.codex); Mobarchy never handles tokens. Every
agent gets the phone's MCP server (mobarchy-mcp) as its tools.
"""

from __future__ import annotations

import asyncio
import json
import os
import shutil
import tempfile
from collections.abc import AsyncIterator
from dataclasses import asdict, dataclass, field
from pathlib import Path

from .config import Config
from .runtime import SYSTEM_PROMPT, Event, EventHandler, _describe_tool_call, _emit, run_task

STATE_FILE = Path.home() / ".config/mobarchy/agent-state.json"
# Where agents run: their own directory, so no project files are picked up.
WORKDIR = Path.home() / ".local/share/mobarchy/agent"


@dataclass
class Status:
    id: str
    name: str
    installed: bool
    ready: bool          # can run a task now
    detail: str = ""     # e.g. the account, or what is missing
    available: bool = True  # supported in this build


# --- state ---------------------------------------------------------------------------------

@dataclass
class State:
    active: str | None = None
    sessions: dict[str, str] = field(default_factory=dict)  # agent -> conversation to resume

    @classmethod
    def load(cls) -> State:
        try:
            data = json.loads(STATE_FILE.read_text())
            return cls(active=data.get("active"), sessions=dict(data.get("sessions", {})))
        except (OSError, ValueError):
            return cls()

    def save(self) -> None:
        STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
        tmp = STATE_FILE.with_suffix(".tmp")
        tmp.write_text(json.dumps(asdict(self), indent=2))
        tmp.replace(STATE_FILE)


# --- Claude (Claude Code) -------------------------------------------------------------------

CLAUDE_INSTALLER = "https://claude.ai/install.sh"


def claude_bin() -> str | None:
    found = shutil.which("claude")
    if found:
        return found
    local = Path.home() / ".local/bin/claude"
    return str(local) if local.exists() else None


async def _run(*cmd: str, env: dict | None = None, timeout: float = 30) -> tuple[int, str]:
    proc = await asyncio.create_subprocess_exec(
        *cmd, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT,
        stdin=asyncio.subprocess.DEVNULL, env=env)
    try:
        out, _ = await asyncio.wait_for(proc.communicate(), timeout)
    except TimeoutError:
        proc.kill()
        return -1, "timed out"
    return proc.returncode or 0, out.decode(errors="replace")


async def claude_status() -> Status:
    s = Status("claude", "Claude", installed=False, ready=False)
    exe = claude_bin()
    if not exe:
        s.detail = "Claude Code is not installed"
        return s
    s.installed = True
    code, out = await _run(exe, "auth", "status", "--json")
    try:
        auth = json.loads(out)
    except ValueError:
        s.detail = "cannot read the Claude Code login state"
        return s
    s.ready = bool(auth.get("loggedIn"))
    if s.ready:
        who = auth.get("email") or auth.get("account", {}).get("email") or ""
        plan = auth.get("subscriptionType") or auth.get("authMethod") or ""
        s.detail = " · ".join(x for x in (who, plan) if x)
    else:
        s.detail = "not logged in"
    return s


def parse_claude_stream(line: str) -> tuple[list[Event], str | None]:
    """One line of `claude -p --output-format stream-json` -> events, and the session id
    if the line carries one."""
    try:
        msg = json.loads(line)
    except ValueError:
        return [], None
    session = msg.get("session_id")
    kind = msg.get("type")
    events: list[Event] = []
    if kind == "assistant":
        for block in msg.get("message", {}).get("content", []):
            if block.get("type") == "text" and block.get("text", "").strip():
                events.append(Event("text", block["text"].strip()))
            elif block.get("type") == "tool_use":
                name = block.get("name", "")
                name = name.removeprefix("mcp__mobarchy__")
                events.append(Event("tool", _describe_tool_call(name, block.get("input") or {})))
    elif kind == "result":
        text = (msg.get("result") or "").strip()
        if msg.get("is_error") or msg.get("subtype", "success") != "success":
            events.append(Event("error", text or f"Claude stopped ({msg.get('subtype')})"))
        else:
            events.append(Event("done", text))
    return events, session


async def claude_run(prompt: str, config: Config, on_event: EventHandler, resume: str | None) -> str | None:
    exe = claude_bin()
    if not exe:
        await _emit(on_event, Event("error", "Claude Code is not installed. Choose Claude to set it up."))
        return None
    WORKDIR.mkdir(parents=True, exist_ok=True)
    mcp = {"mcpServers": {"mobarchy": {"type": "http", "url": config.mcp_url}}}
    cmd = [exe, "-p", prompt, "--output-format", "stream-json", "--verbose",
           # The phone's tools; no shell or file tools of Claude Code's own.
           "--tools", "", "--mcp-config", json.dumps(mcp), "--allowedTools", "mcp__mobarchy",
           "--permission-mode", "dontAsk", "--append-system-prompt", SYSTEM_PROMPT]
    if resume:
        cmd += ["--resume", resume]
    proc = await asyncio.create_subprocess_exec(
        *cmd, cwd=WORKDIR, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
        stdin=asyncio.subprocess.DEVNULL, limit=16 * 1024 * 1024)
    session = None
    finished = False
    assert proc.stdout is not None
    async for raw in proc.stdout:
        events, sid = parse_claude_stream(raw.decode(errors="replace"))
        session = sid or session
        for e in events:
            finished |= e.kind in ("done", "error")
            await _emit(on_event, e)
    err = (await proc.stderr.read()).decode(errors="replace").strip() if proc.stderr else ""
    code = await proc.wait()
    if not finished:
        await _emit(on_event, Event("error", err.splitlines()[-1] if err else f"Claude Code exited ({code})"))
    return session


async def _stream_lines(proc: asyncio.subprocess.Process) -> AsyncIterator[str]:
    assert proc.stdout is not None
    buf = b""
    while True:
        chunk = await proc.stdout.read(256)
        if not chunk:
            break
        buf += chunk
        # Installers and login prompts print partial lines ("Paste code here >").
        while b"\n" in buf or b"\r" in buf:
            cut = min(i for i in (buf.find(b"\n"), buf.find(b"\r")) if i >= 0)
            line, buf = buf[:cut], buf[cut + 1:]
            yield line.decode(errors="replace")
    if buf:
        yield buf.decode(errors="replace")


async def claude_setup(on_event: EventHandler) -> bool:
    """Install Claude Code if needed (Anthropic's own installer), then log in with Claude
    Code's own login, which opens the browser and completes there (localhost callback)."""
    if not claude_bin():
        await _emit(on_event, Event("text", "Installing Claude Code (from Anthropic) …"))
        with tempfile.TemporaryDirectory() as tmp:
            script = Path(tmp) / "install.sh"
            code, out = await _run("curl", "-fsSL", CLAUDE_INSTALLER, "-o", str(script), timeout=60)
            if code != 0:
                await _emit(on_event, Event("error", "Cannot download the Claude Code installer (is the phone online?)."))
                return False
            code, out = await _run("bash", str(script), timeout=600)
            if code != 0 or not claude_bin():
                last = out.strip().splitlines()[-1] if out.strip() else "unknown error"
                await _emit(on_event, Event("error", f"Installing Claude Code failed: {last}"))
                return False
        await _emit(on_event, Event("tool", "Claude Code installed"))
    if (await claude_status()).ready:
        return True

    await _emit(on_event, Event("text", "Opening the Claude login page in the browser. Log in there, then come back."))
    env = dict(os.environ, BROWSER="mobarchy-open")
    proc = await asyncio.create_subprocess_exec(
        claude_bin(), "auth", "login", stdin=asyncio.subprocess.DEVNULL,
        stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT, env=env)
    try:
        async def watch():
            async for line in _stream_lines(proc):
                if "visit:" in line:
                    await _emit(on_event, Event("url", line.split("visit:", 1)[1].strip()))
            return await proc.wait()
        await asyncio.wait_for(watch(), 900)
    except TimeoutError:
        proc.kill()
        await _emit(on_event, Event("error", "The login was not completed in time."))
        return False
    return (await claude_status()).ready


# --- OpenAI (Codex) -------------------------------------------------------------------------

CODEX_RELEASE = ("https://github.com/openai/codex/releases/latest/download/"
                 "codex-{arch}-unknown-linux-musl.tar.gz")


def codex_bin() -> str | None:
    found = shutil.which("codex")
    if found:
        return found
    local = Path.home() / ".local/bin/codex"
    return str(local) if local.exists() else None


async def codex_status() -> Status:
    s = Status("openai", "OpenAI", installed=False, ready=False)
    exe = codex_bin()
    if not exe:
        s.detail = "Codex is not installed"
        return s
    s.installed = True
    code, out = await _run(exe, "login", "status")
    text = out.strip().splitlines()[-1] if out.strip() else ""
    s.ready = code == 0 and "not logged in" not in text.lower()
    # "Logged in using ChatGPT" / "Logged in using an API key - sk-…": keep the method only.
    s.detail = text.split(" - ")[0].removeprefix("Logged in using ") if s.ready else "not logged in"
    return s


def parse_codex_stream(line: str) -> tuple[list[Event], str | None]:
    """One line of `codex exec --json` -> events, and the thread id if the line has one."""
    try:
        msg = json.loads(line)
    except ValueError:
        return [], None
    kind = msg.get("type", "")
    thread = msg.get("thread_id") if kind == "thread.started" else None
    events: list[Event] = []
    item = msg.get("item") or {}
    if kind == "item.completed" and item.get("type") == "agent_message" and item.get("text", "").strip():
        events.append(Event("text", item["text"].strip()))
    elif kind == "item.started" and item.get("type") == "mcp_tool_call":
        events.append(Event("tool", _describe_tool_call(item.get("tool", ""), item.get("arguments") or {})))
    elif kind == "item.started" and item.get("type") == "command_execution":
        events.append(Event("tool", f"$ {item.get('command', '')}"))
    elif kind == "turn.completed":
        # Codex has no final-answer event: its last message (already shown) is the answer.
        events.append(Event("done", ""))
    elif kind == "turn.failed":
        events.append(Event("error", (msg.get("error") or {}).get("message", "Codex failed")))
    elif kind == "error":
        events.append(Event("error", msg.get("message", "Codex error")))
    return events, thread


async def codex_run(prompt: str, config: Config, on_event: EventHandler, resume: str | None) -> str | None:
    exe = codex_bin()
    if not exe:
        await _emit(on_event, Event("error", "Codex is not installed. Choose OpenAI to set it up."))
        return None
    WORKDIR.mkdir(parents=True, exist_ok=True)
    args = ["--json", "--skip-git-repo-check",
            # The phone's tools. Codex's own shell runs read-only and never asks.
            "-c", f'mcp_servers.mobarchy.url="{config.mcp_url}"',
            "-c", 'approval_policy="never"', "-s", "read-only",
            "-c", "instructions=" + json.dumps(SYSTEM_PROMPT)]
    cmd = [exe, "exec", *args, "resume", resume, prompt] if resume else [exe, "exec", *args, prompt]
    proc = await asyncio.create_subprocess_exec(
        *cmd, cwd=WORKDIR, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
        stdin=asyncio.subprocess.DEVNULL, limit=16 * 1024 * 1024)
    thread = resume
    finished = False
    assert proc.stdout is not None
    async for raw in proc.stdout:
        events, tid = parse_codex_stream(raw.decode(errors="replace"))
        thread = tid or thread
        for e in events:
            finished |= e.kind in ("done", "error")
            await _emit(on_event, e)
    err = (await proc.stderr.read()).decode(errors="replace").strip() if proc.stderr else ""
    code = await proc.wait()
    if not finished:
        await _emit(on_event, Event("error", err.splitlines()[-1] if err else f"Codex exited ({code})"))
    return thread


async def codex_setup(on_event: EventHandler) -> bool:
    """Install Codex if needed (OpenAI's release binary), then log in with Codex's own
    login, which opens the browser and completes there (localhost callback)."""
    if not codex_bin():
        await _emit(on_event, Event("text", "Installing Codex (from OpenAI) …"))
        arch = os.uname().machine
        dest = Path.home() / ".local/bin"
        dest.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory() as tmp:
            tgz = Path(tmp) / "codex.tgz"
            code, _ = await _run("curl", "-fsSL", "-o", str(tgz), CODEX_RELEASE.format(arch=arch), timeout=300)
            if code != 0:
                await _emit(on_event, Event("error", "Cannot download Codex (is the phone online?)."))
                return False
            code, out = await _run("tar", "-xzf", str(tgz), "-C", tmp)
            binary = Path(tmp) / f"codex-{arch}-unknown-linux-musl"
            if code != 0 or not binary.exists():
                await _emit(on_event, Event("error", f"Unpacking Codex failed: {out.strip()}"))
                return False
            shutil.copy(binary, dest / "codex")
            (dest / "codex").chmod(0o755)
        await _emit(on_event, Event("tool", "Codex installed"))
    if (await codex_status()).ready:
        return True

    await _emit(on_event, Event("text", "Opening the OpenAI login page in the browser. Log in there, then come back."))
    env = dict(os.environ, BROWSER="mobarchy-open")
    proc = await asyncio.create_subprocess_exec(
        codex_bin(), "login", stdin=asyncio.subprocess.DEVNULL,
        stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT, env=env)
    try:
        async def watch():
            async for line in _stream_lines(proc):
                url = next((w for w in line.split() if w.startswith("https://auth.openai.com")), None)
                if url:
                    await _emit(on_event, Event("url", url))
            return await proc.wait()
        await asyncio.wait_for(watch(), 900)
    except TimeoutError:
        proc.kill()
        await _emit(on_event, Event("error", "The login was not completed in time."))
        return False
    return (await codex_status()).ready


# --- the others -----------------------------------------------------------------------------

async def api_status(config: Config) -> Status:
    ready = config.has_credentials()
    return Status("api", "Anthropic API", installed=True, ready=ready,
                  detail="API key set" if ready else "no ANTHROPIC_API_KEY in ~/.config/mobarchy/agent.env")


async def statuses(config: Config) -> list[Status]:
    return [
        await claude_status(),
        await codex_status(),
        Status("local", "Local agent", installed=False, ready=False, detail="coming soon", available=False),
        await api_status(config),
    ]


async def setup(agent: str, on_event: EventHandler) -> bool:
    if agent == "claude":
        ok = await claude_setup(on_event)
    elif agent == "openai":
        ok = await codex_setup(on_event)
    else:
        await _emit(on_event, Event("error", f"Setting up {agent} is not supported yet."))
        return False
    if ok:
        st = State.load()
        st.active = agent
        st.save()
        s = await (claude_status() if agent == "claude" else codex_status())
        await _emit(on_event, Event("done", f"{s.name} is ready{(' (' + s.detail + ')') if s.detail else ''}."))
    return ok


async def run(prompt: str, config: Config, on_event: EventHandler, agent: str | None = None,
              new_conversation: bool = False) -> None:
    st = State.load()
    agent = agent or st.active
    if not agent:
        await _emit(on_event, Event("error", "No agent chosen yet. Choose one on the home screen."))
        return
    resume = None if new_conversation else st.sessions.get(agent)
    if agent == "claude":
        session = await claude_run(prompt, config, on_event, resume)
        if session:
            st = State.load()
            st.sessions[agent] = session
            st.save()
    elif agent == "openai":
        thread = await codex_run(prompt, config, on_event, resume)
        if thread:
            st = State.load()
            st.sessions[agent] = thread
            st.save()
    elif agent == "api":
        await run_task(prompt, config, on_event)
    else:
        await _emit(on_event, Event("error", f"The {agent} agent is not available yet."))
