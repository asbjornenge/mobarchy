"""Asking the owner (ADR 0024): a tool whose policy is `ask` runs only when the owner allows
it on the screen. The shell shows the sheet (org.mobarchy.Approvals on the session bus) and
answers once the owner has chosen: this call, this tool for the rest of the task (the
agent's MCP session), or no. No shell, or no answer within two minutes, is a no."""

from __future__ import annotations

import enum
import json
import subprocess

BUS = ["org.mobarchy.Shell", "/org/mobarchy/Approvals", "org.mobarchy.Approvals"]
TIMEOUT_S = 120


class Answer(enum.StrEnum):
    ONCE = "once"
    TASK = "task"
    DENY = "deny"


class ApprovalError(Exception):
    pass


def detail(arguments: dict) -> str:
    """What the sheet shows of the call: a command as it is, anything else as its arguments."""
    if isinstance(arguments.get("command"), str):
        return arguments["command"]
    return json.dumps(arguments, ensure_ascii=False, indent=1) if arguments else ""


def ask(tool: str, title: str, what: str) -> Answer:
    r = subprocess.run(["busctl", "--user", "--json=short", f"--timeout={TIMEOUT_S}", "call",
                        *BUS, "Ask", "sss", tool, title, what],
                       capture_output=True, text=True, timeout=TIMEOUT_S + 10)
    if r.returncode != 0:
        raise ApprovalError(r.stderr.strip() or "the shell did not answer")
    answer = json.loads(r.stdout)["data"][0]
    return Answer(answer) if answer in Answer._value2member_map_ else Answer.DENY
