"""The agents the owner can choose between (ADR 0010), behind one event stream.

    claude   the owner's own Claude Code (their Claude account), `claude -p`
    openai   the owner's own Codex (their OpenAI account), `codex exec --json`
    local    a small model on the phone (llama.cpp), the offline fallback (local.py)
    api      the Anthropic API with a key (runtime.py)

Logins belong to the tools (~/.claude, ~/.codex); Mobarchy never handles tokens. Every
agent gets the phone's MCP server (mobarchy-mcp) as its tools.
"""

from __future__ import annotations

import asyncio
import json
import os
import re
import shutil
import socket
import sys
import tempfile
import time
import uuid
from collections.abc import AsyncIterator
from dataclasses import asdict, dataclass, replace
from pathlib import Path

from . import local
from .config import Config
from .conversations import Conversation
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
    conversation: str | None = None  # the current one (conversations.py)

    @classmethod
    def load(cls) -> State:
        try:
            data = json.loads(STATE_FILE.read_text())
            return cls(active=data.get("active"), conversation=data.get("conversation"))
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


# The owner's MCP servers in Claude Code (claude.ai connectors such as Gmail, and any they add):
# the agent may use them. dontAsk needs a rule per server (no wildcards there); `claude mcp
# list` names them, slowly (it checks each), so the list is kept for a while.
CONNECTORS_CACHE = Path.home() / ".cache/mobarchy/claude-mcp-servers.json"
CONNECTORS_MAX_AGE = 6 * 3600


def parse_mcp_list(output: str) -> list[str]:
    """`claude mcp list` -> the servers' tool prefixes ("claude.ai Google Drive: https://... - ✔
    Connected" -> "mcp__claude_ai_Google_Drive"). Mobarchy's own (mobarchy) is left out."""
    servers = []
    for line in output.splitlines():
        m = re.match(r"^(.+?): (?:https?://|/|\S+ ).* - ", line)
        if not m:
            continue
        name = re.sub(r"[^A-Za-z0-9_-]", "_", m.group(1).strip())
        if name != "mobarchy":
            servers.append(f"mcp__{name}")
    return servers


async def _refresh_connectors(exe: str) -> list[str]:
    code, out = await _run(exe, "mcp", "list", timeout=90)
    servers = parse_mcp_list(out) if code == 0 else []
    if code == 0:
        CONNECTORS_CACHE.parent.mkdir(parents=True, exist_ok=True)
        CONNECTORS_CACHE.write_text(json.dumps(servers))
    return servers


async def claude_connectors(exe: str) -> list[str]:
    """The owner's MCP servers' allow rules: from the cache (refreshed in the background when
    old), or asked for now the first time."""
    try:
        servers = json.loads(CONNECTORS_CACHE.read_text())
        if time.time() - CONNECTORS_CACHE.stat().st_mtime > CONNECTORS_MAX_AGE:
            asyncio.get_running_loop().create_task(_refresh_connectors(exe))
        return servers
    except (OSError, ValueError):
        return await _refresh_connectors(exe)


async def claude_command(exe: str, config: Config, system_prompt: str = SYSTEM_PROMPT) -> list[str]:
    """Claude Code as the phone's agent, up to its prompt (`-p PROMPT`, or stream-json input
    for a conversation that stays open: Frigg, ADR 0026)."""
    mcp = {"mcpServers": {"mobarchy": {"type": "http", "url": config.mcp_url}}}
    # The phone's tools; Claude Code's file tools for its own memory only (what it keeps
    # about the owner across conversations); the web (search and read pages: e.g. an image
    # for a theme's wallpaper, ADR 0025); and the owner's own MCP servers (claude.ai
    # connectors: Gmail, Calendar, ...). Everything else is denied (dontAsk).
    memory = "~/.claude/projects/**/memory/**"
    return [exe, "--output-format", "stream-json", "--verbose",
            "--tools", "Read,Write,Edit,WebSearch,WebFetch", "--mcp-config", json.dumps(mcp),
            "--allowedTools", "mcp__mobarchy", f"Read({memory})", f"Write({memory})", f"Edit({memory})",
            "WebSearch", "WebFetch", *(await claude_connectors(exe)),
            "--permission-mode", "dontAsk", "--append-system-prompt", system_prompt]


GUEST = Path.home() / ".local/share/mobarchy/frigg-guest"


def claude_guest(exe: str, config: Config, system_prompt: str) -> tuple[list[str], dict[str, str], Path]:
    """Claude Code for a voice Frigg didn't recognise on a locked phone (ADR 0026): (command,
    environment, working dir). A configuration of its own (the owner's login only: none of
    their memory, history or MCP servers), the web, and the phone's harmless tools
    (mobarchy-mcp checks ?voice=guest)."""
    config_dir = GUEST / "config"
    config_dir.mkdir(parents=True, exist_ok=True)
    (GUEST / "work").mkdir(exist_ok=True)
    credentials = config_dir / ".credentials.json"
    if not credentials.is_symlink():
        credentials.unlink(missing_ok=True)
        credentials.symlink_to(Path.home() / ".claude/.credentials.json")
    url = f"{config.mcp_url}{'&' if '?' in config.mcp_url else '?'}voice=guest"
    mcp = {"mcpServers": {"mobarchy": {"type": "http", "url": url}}}
    cmd = [exe, "--output-format", "stream-json", "--verbose",
           "--tools", "WebSearch,WebFetch", "--mcp-config", json.dumps(mcp), "--strict-mcp-config",
           "--allowedTools", "mcp__mobarchy", "WebSearch", "WebFetch",
           "--permission-mode", "dontAsk", "--append-system-prompt", system_prompt]
    return cmd, {**os.environ, "CLAUDE_CONFIG_DIR": str(config_dir)}, GUEST / "work"


async def claude_run(prompt: str, config: Config, on_event: EventHandler, resume: str | None) -> str | None:
    exe = claude_bin()
    if not exe:
        await _emit(on_event, Event("error", "Claude Code is not installed. Choose Claude to set it up."))
        return None
    WORKDIR.mkdir(parents=True, exist_ok=True)
    cmd = [*await claude_command(exe, config), "-p", prompt]
    if resume:
        cmd += ["--resume", resume]
    # (a step the owner approves waits for them: up to two minutes, then root)
    env = {**os.environ, "MCP_TOOL_TIMEOUT": "600000"}
    proc = await asyncio.create_subprocess_exec(
        *cmd, cwd=WORKDIR, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
        stdin=asyncio.subprocess.DEVNULL, limit=16 * 1024 * 1024, env=env)
    session = None
    finished = False
    last_text = ""
    assert proc.stdout is not None
    async for raw in proc.stdout:
        events, sid = parse_claude_stream(raw.decode(errors="replace"))
        session = sid or session
        for e in events:
            if e.kind == "text":
                last_text = e.text
            elif e.kind == "done" and e.text == last_text:
                e = Event("done", "")  # the final message was already shown as text
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
        claude_bin(), "auth", "login", stdin=asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT, env=env)
    return await _login(proc, on_event, url_marker="visit:", wants_code=True) and (await claude_status()).ready


async def _forward_stdin(proc: asyncio.subprocess.Process) -> None:
    """Lines on our stdin (the shell's code field) go to the login process: the manual login
    flow ends on a page with a code to paste into the tool."""
    loop = asyncio.get_running_loop()
    reader = asyncio.StreamReader()
    await loop.connect_read_pipe(lambda: asyncio.StreamReaderProtocol(reader), sys.stdin)
    while line := await reader.readline():
        if proc.stdin and proc.returncode is None:
            proc.stdin.write(line)
            await proc.stdin.drain()


async def _login(proc: asyncio.subprocess.Process, on_event: EventHandler, url_marker: str,
                 wants_code: bool) -> bool:
    forward = asyncio.create_task(_forward_stdin(proc)) if wants_code else None

    async def watch():
        async for line in _stream_lines(proc):
            if url_marker in line:
                url = line.split(url_marker, 1)[1].strip().split()[0] if url_marker.endswith(":") \
                    else next((w for w in line.split() if w.startswith(url_marker)), "")
                if url:
                    await _emit(on_event, Event("url", url))
            elif wants_code and "paste code" in line.lower():
                await _emit(on_event, Event("code", "If the login page shows a code, paste it here."))
        return await proc.wait()

    try:
        await asyncio.wait_for(watch(), 900)
        return proc.returncode == 0
    except TimeoutError:
        proc.kill()
        await _emit(on_event, Event("error", "The login was not completed in time."))
        return False
    finally:
        if forward:
            forward.cancel()


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
            # (a step the owner approves waits for them: up to two minutes, then root)
            "-c", "mcp_servers.mobarchy.tool_timeout_sec=600",
            "-c", 'approval_policy="never"', "-s", "read-only",
            # (and the web: search, e.g. an image for a theme's wallpaper)
            "-c", "tools.web_search=true",
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
    # Codex's page always calls back to localhost: no code to paste.
    return await _login(proc, on_event, url_marker="https://auth.openai.com", wants_code=False) \
        and (await codex_status()).ready


# --- local ----------------------------------------------------------------------------------

async def local_status() -> Status:
    s = Status("local", "Local agent", installed=local.installed(), ready=local.installed())
    if s.ready:
        s.detail = f"{local.MODEL_NAME} on the phone · works offline"
    elif not local.server_bin():
        s.detail = "llama.cpp is not installed"
        s.available = False
    else:
        s.detail = f"downloads {local.MODEL_NAME} (2.5 GB)"
    return s


async def local_run(prompt: str, config: Config, on_event: EventHandler, conv: Conversation) -> None:
    """The local model has no session of its own: it gets the conversation so far (every
    agent's turns) as its history."""
    history = [{"role": "system", "content": local.LOCAL_PROMPT}]
    for t in conv.turns[:-1]:  # the last turn is this prompt
        if t.role == "user":
            history.append({"role": "user", "content": t.text})
        elif t.role == "assistant":
            history.append({"role": "assistant", "content": t.text})
    await local.run(prompt, config, on_event, history[-41:] if len(history) > 41 else history)


def online(timeout: float = 2.0) -> bool:
    """A quick reachability check (no DNS): can we open a connection to the internet?"""
    for host in ("1.1.1.1", "8.8.8.8"):
        try:
            with socket.create_connection((host, 443), timeout=timeout):
                return True
        except OSError:
            continue
    return False


# --- the others -----------------------------------------------------------------------------

async def api_status(config: Config) -> Status:
    ready = config.has_credentials()
    return Status("api", "Anthropic API", installed=True, ready=ready,
                  detail="API key set" if ready else "no ANTHROPIC_API_KEY in ~/.config/mobarchy/agent.env")


async def statuses(config: Config) -> list[Status]:
    return [
        await claude_status(),
        await codex_status(),
        await local_status(),
        await api_status(config),
    ]


async def setup(agent: str, on_event: EventHandler) -> bool:
    if agent == "claude":
        ok = await claude_setup(on_event)
    elif agent == "openai":
        ok = await codex_setup(on_event)
    elif agent == "local":
        ok = await local.setup(on_event)
    else:
        await _emit(on_event, Event("error", f"Setting up {agent} is not supported yet."))
        return False
    if ok:
        st = State.load()
        st.active = agent
        st.save()
        s = await {"claude": claude_status, "openai": codex_status, "local": local_status}[agent]()
        await _emit(on_event, Event("done", f"{s.name} is ready{(' (' + s.detail + ')') if s.detail else ''}."))
    return ok


def current_conversation(new: bool = False) -> Conversation:
    st = State.load()
    conv = None if new or not st.conversation else Conversation.load(st.conversation)
    if conv is None:
        conv = Conversation()
        conv.save()     # (at once: the chat and Frigg must find the same one)
        st.conversation = conv.id
        st.save()
    return conv


def task_url(url: str) -> str:
    """The MCP server's URL naming a new task (mobarchy-mcp keeps "allowed for this task" per task)."""
    return f"{url}{'&' if '?' in url else '?'}task={uuid.uuid4().hex}"


async def run(prompt: str, config: Config, on_event: EventHandler, agent: str | None = None,
              new_conversation: bool = False) -> None:
    st = State.load()
    agent = agent or st.active
    if not agent:
        await _emit(on_event, Event("error", "No agent chosen yet. Choose one on the home screen."))
        return
    # Without a network, the cloud agents can't answer: the local one does (if it is there).
    if agent in ("claude", "openai", "api") and not online() and local.installed():
        await _emit(on_event, Event("tool", "offline: the local agent answers"))
        agent = "local"

    conv = current_conversation(new_conversation)
    conv.add("user", prompt, agent)
    conv.save()

    # Everything the agent says or does goes into the conversation as well.
    async def record(e: Event) -> None:
        role = {"text": "assistant", "tool": "tool", "error": "error"}.get(e.kind)
        if e.kind == "done" and e.text:
            role = "assistant"
        if role:
            conv.add(role, e.text, agent)
            conv.save()
        await _emit(on_event, e)

    # This run is a task of its own to the phone's tools: what the owner allows "for this
    # task" ends with it (ADR 0024).
    config = replace(config, mcp_url=task_url(config.mcp_url))
    resume = conv.sessions.get(agent)
    bridged = conv.bridged_prompt(agent, prompt)
    session = None
    if agent == "local":
        await local_run(prompt, config, record, conv)
    elif agent == "claude":
        session = await claude_run(bridged, config, record, resume)
    elif agent == "openai":
        session = await codex_run(bridged, config, record, resume)
    elif agent == "api":
        await run_task(bridged, config, record)
    else:
        await _emit(on_event, Event("error", f"The {agent} agent is not available yet."))
    if session:
        conv.sessions[agent] = session
    conv.seen[agent] = len(conv.turns)
    conv.save()
