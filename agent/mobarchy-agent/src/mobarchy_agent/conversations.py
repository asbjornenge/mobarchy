"""Conversations belong to Mobarchy, not to an agent (ADR 0010).

Every turn (what the owner asked, what the agent said, the tools it used) is kept here,
whichever agent answered. Switching agents mid-conversation hands the new agent what it
has not seen yet; switching back resumes that agent's own session with only the turns it
missed. The shell shows a conversation from here, so it survives restarts.

    ~/.local/share/mobarchy/conversations/<id>.json
"""

from __future__ import annotations

import json
import time
import uuid
from dataclasses import asdict, dataclass, field
from pathlib import Path

ROOT = Path.home() / ".local/share/mobarchy/conversations"
# What another agent's turns may add to a prompt, at most (characters).
BRIDGE_LIMIT = 12000


@dataclass
class Turn:
    role: str    # "user" | "assistant" | "tool" | "error" | "note"
    text: str
    agent: str = ""
    ts: float = field(default_factory=time.time)


@dataclass
class Conversation:
    id: str = field(default_factory=lambda: uuid.uuid4().hex[:12])
    created: float = field(default_factory=time.time)
    updated: float = field(default_factory=time.time)
    turns: list[Turn] = field(default_factory=list)
    sessions: dict[str, str] = field(default_factory=dict)  # agent -> its own session id
    seen: dict[str, int] = field(default_factory=dict)      # agent -> turns it has seen

    @property
    def title(self) -> str:
        first = next((t.text for t in self.turns if t.role == "user"), "")
        return (first[:60] + "…") if len(first) > 60 else first

    @classmethod
    def load(cls, cid: str) -> Conversation | None:
        try:
            data = json.loads((ROOT / f"{cid}.json").read_text())
        except (OSError, ValueError):
            return None
        data["turns"] = [Turn(**t) for t in data.get("turns", [])]
        return cls(**data)

    def save(self) -> None:
        ROOT.mkdir(parents=True, exist_ok=True)
        self.updated = time.time()
        tmp = ROOT / f"{self.id}.json.tmp"
        tmp.write_text(json.dumps(asdict(self), ensure_ascii=False))
        tmp.replace(ROOT / f"{self.id}.json")

    def add(self, role: str, text: str, agent: str = "") -> None:
        self.turns.append(Turn(role, text, agent))

    def unseen_by(self, agent: str, upto: int | None = None) -> list[Turn]:
        """Turns of this conversation the agent has not seen (other agents' work, mostly)."""
        end = len(self.turns) if upto is None else upto
        return [t for t in self.turns[self.seen.get(agent, 0):end]
                if t.role in ("user", "assistant", "tool") and not (t.agent == agent and t.role != "user")]

    def bridged_prompt(self, agent: str, prompt: str) -> str:
        """The owner's prompt, with what the agent missed in front of it."""
        missed = self.unseen_by(agent, upto=len(self.turns) - 1)  # the last turn is this prompt
        if not missed:
            return prompt
        lines = []
        for t in missed:
            who = {"user": "Owner", "assistant": f"Assistant ({t.agent or 'agent'})",
                   "tool": f"Tool call ({t.agent or 'agent'})"}[t.role]
            lines.append(f"{who}: {t.text}")
        transcript = "\n".join(lines)
        if len(transcript) > BRIDGE_LIMIT:
            transcript = "…\n" + transcript[-BRIDGE_LIMIT:]
        return ("Earlier in this conversation, while another assistant was answering:\n"
                f"{transcript}\n\nThe owner now says:\n{prompt}")


def all_conversations() -> list[Conversation]:
    convs = [c for p in ROOT.glob("*.json") if (c := Conversation.load(p.stem))]
    return sorted(convs, key=lambda c: c.updated, reverse=True)
