"""Risk levels and the permission policy (ADR 0004).

Every tool declares a risk level. The policy decides, per level and per tool,
whether a call is allowed. It is enforced here in the tool layer, so a
misbehaving agent cannot bypass it.

Policy file (TOML), default /etc/mobarchy/policy.toml:

    [risk]
    read = "allow"
    act = "allow"
    confirm = "ask"        # the owner answers on the screen (ADR 0024)

    [tools]
    run_shell = "allow"    # per-tool override
"""

from __future__ import annotations

import enum
import os
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

DEFAULT_POLICY_PATH = Path("/etc/mobarchy/policy.toml")


class Risk(enum.StrEnum):
    READ = "read"          # observe only
    ACT = "act"            # changes local state, reversible
    CONFIRM = "confirm"    # irreversible or outward-facing: needs the user's consent


class Decision(enum.StrEnum):
    ALLOW = "allow"
    ASK = "ask"            # the owner decides, call by call (approval.py, ADR 0024)
    DENY = "deny"


DEFAULTS = {Risk.READ: Decision.ALLOW, Risk.ACT: Decision.ALLOW, Risk.CONFIRM: Decision.ASK}


@dataclass
class Policy:
    risk: dict[Risk, Decision] = field(default_factory=lambda: dict(DEFAULTS))
    tools: dict[str, Decision] = field(default_factory=dict)
    source: str = "built-in defaults"

    def decide(self, tool: str, risk: Risk) -> Decision:
        return self.tools.get(tool, self.risk[risk])

    @classmethod
    def load(cls, path: Path | None = None) -> Policy:
        path = path or Path(os.environ.get("MOBARCHY_POLICY", DEFAULT_POLICY_PATH))
        if not path.exists():
            return cls()
        with path.open("rb") as f:
            data = tomllib.load(f)
        risk = dict(DEFAULTS)
        for k, v in data.get("risk", {}).items():
            risk[Risk(k)] = Decision(v)
        tools = {k: Decision(v) for k, v in data.get("tools", {}).items()}
        return cls(risk=risk, tools=tools, source=str(path))


class PolicyDenied(Exception):
    pass
