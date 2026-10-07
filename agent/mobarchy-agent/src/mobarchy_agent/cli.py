"""mobarchy-agent: give the phone a task, and manage which agent does it (ADR 0010).

    mobarchy-agent [--json] [--agent AGENT] [--new] "open a terminal and show the uptime"
    mobarchy-agent agents [--json]          the agents, and which ones are ready
    mobarchy-agent use AGENT                make AGENT the active one
    mobarchy-agent setup AGENT [--json]     install / log in (opens the browser)
    mobarchy-agent conversations [--json]   the conversations, newest first
    mobarchy-agent conversation new|open ID|show [ID] [--json]
    mobarchy-agent transcribe FILE.wav [--json] [--language no]   speech to text, on the phone
    mobarchy-agent setup voice              download the speech model
    mobarchy-agent talk [--wake]            Frigg: a live voice conversation (ADR 0026), JSON events;
                                            --wake: waits for "Hei Frigg" between conversations;
                                            --enroll: learns the owner's voice (Voice Match)
    mobarchy-agent setup talk               download its voice models (VAD, Piper voices)

Agents: claude (your Claude Code and Claude account), openai, local, api (API key).
"""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
import wave
from dataclasses import asdict
from pathlib import Path

from . import agents, voice
from .config import Config
from .conversations import Conversation, all_conversations
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
    p.add_argument("agent", choices=["claude", "openai", "local", "api", "voice", "talk"])
    p.add_argument("--json", action="store_true")
    args = p.parse_args(argv)
    if args.agent == "voice":
        voice.setup()
        return 0
    if args.agent == "talk":
        from . import talk
        talk.setup()
        return 0
    show, failed = _printer(args.json)
    ok = asyncio.run(agents.setup(args.agent, show))
    return 0 if ok and not failed() else 1


def cmd_conversations(argv: list[str]) -> int:
    p = argparse.ArgumentParser(prog="mobarchy-agent conversations")
    p.add_argument("--json", action="store_true")
    args = p.parse_args(argv)
    current = agents.State.load().conversation
    items = [{"id": c.id, "title": c.title, "updated": c.updated, "current": c.id == current,
              "agents": sorted({t.agent for t in c.turns if t.agent})}
             for c in all_conversations() if c.turns]
    if args.json:
        print(json.dumps(items, ensure_ascii=False))
    else:
        for i in items:
            print(f"{'*' if i['current'] else ' '} {i['id']}  {i['title']}")
    return 0


def cmd_conversation(argv: list[str]) -> int:
    p = argparse.ArgumentParser(prog="mobarchy-agent conversation")
    p.add_argument("action", choices=["new", "open", "show"])
    p.add_argument("id", nargs="?")
    p.add_argument("--json", action="store_true")
    args = p.parse_args(argv)
    st = agents.State.load()
    if args.action == "new":
        conv = Conversation()
        conv.save()
        st.conversation = conv.id
        st.save()
        return 0
    if args.action == "open":
        if not args.id or not Conversation.load(args.id):
            print("no such conversation", file=sys.stderr)
            return 1
        st.conversation = args.id
        st.save()
        return 0
    conv = Conversation.load(args.id or st.conversation or "")
    turns = [{"role": t.role, "text": t.text, "agent": t.agent} for t in conv.turns] if conv else []
    if args.json:
        print(json.dumps({"id": conv.id if conv else None, "turns": turns}, ensure_ascii=False))
    else:
        for t in turns:
            print(f"[{t['agent'] or '-'}] {t['role']}: {t['text']}")
    return 0


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


def cmd_transcribe(argv: list[str]) -> int:
    p = argparse.ArgumentParser(prog="mobarchy-agent transcribe")
    p.add_argument("file")
    p.add_argument("--language", default="no")
    p.add_argument("--json", action="store_true")
    args = p.parse_args(argv)
    try:
        text = voice.transcribe(Path(args.file), args.language)
    except (RuntimeError, OSError, EOFError, wave.Error) as e:
        print(json.dumps({"error": str(e)}) if args.json else f"✗ {e}", flush=True)
        return 1
    print(json.dumps({"text": text}, ensure_ascii=False) if args.json else text, flush=True)
    return 0


def cmd_talk(argv: list[str]) -> int:
    from . import talk
    p = argparse.ArgumentParser(prog="mobarchy-agent talk")
    p.add_argument("--wake", action="store_true", help='wait for "Hei Frigg" between conversations')
    p.add_argument("--enroll", action="store_true", help="learn the owner's voice (Voice Match)")
    args = p.parse_args(argv)
    return asyncio.run(talk.main(wake=args.wake, enroll=args.enroll))


def main() -> None:
    argv = sys.argv[1:]
    commands = {"agents": cmd_agents, "use": cmd_use, "setup": cmd_setup,
                "conversations": cmd_conversations, "conversation": cmd_conversation,
                "transcribe": cmd_transcribe, "talk": cmd_talk}
    if argv and argv[0] in commands:
        sys.exit(commands[argv[0]](argv[1:]))
    sys.exit(cmd_task(argv))


if __name__ == "__main__":
    main()
