"""Agent configuration.

Read from environment variables, which may be supplied by env files (KEY=VALUE lines).
The first file that exists wins, and real environment variables override file values:

    $MOBARCHY_AGENT_ENV
    ~/.config/mobarchy/agent.env
    /etc/mobarchy/agent.env

Secrets (ANTHROPIC_API_KEY) live only in these files, never in the repo.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

DEFAULT_MODEL = "claude-opus-5"
DEFAULT_MCP_URL = "http://127.0.0.1:8765/mcp"


def _env_files() -> list[Path]:
    files = [Path.home() / ".config/mobarchy/agent.env", Path("/etc/mobarchy/agent.env")]
    if os.environ.get("MOBARCHY_AGENT_ENV"):
        files.insert(0, Path(os.environ["MOBARCHY_AGENT_ENV"]))
    return files


def _read_env_file(path: Path) -> dict[str, str]:
    values = {}
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        values[k.strip()] = v.strip().strip('"').strip("'")
    return values


@dataclass
class Config:
    model: str = DEFAULT_MODEL
    effort: str = "medium"
    mcp_url: str = DEFAULT_MCP_URL
    max_iterations: int = 40
    source: str | None = None

    def has_credentials(self) -> bool:
        """True if the SDK can find credentials: an env var or an `ant auth login` profile."""
        if os.environ.get("ANTHROPIC_API_KEY") or os.environ.get("ANTHROPIC_AUTH_TOKEN"):
            return True
        return (Path.home() / ".config/anthropic").is_dir()

    @classmethod
    def load(cls) -> Config:
        values: dict[str, str] = {}
        source = None
        for f in _env_files():
            if f.is_file():
                values = _read_env_file(f)
                source = str(f)
                break
        # Credentials go to the SDK through the process environment.
        for key in ("ANTHROPIC_API_KEY", "ANTHROPIC_AUTH_TOKEN", "ANTHROPIC_BASE_URL"):
            if key in values and key not in os.environ:
                os.environ[key] = values[key]

        def get(key: str, default: str) -> str:
            return os.environ.get(key, values.get(key, default))

        return cls(
            model=get("MOBARCHY_AGENT_MODEL", DEFAULT_MODEL),
            effort=get("MOBARCHY_AGENT_EFFORT", "medium"),
            mcp_url=get("MOBARCHY_MCP_URL", DEFAULT_MCP_URL),
            max_iterations=int(get("MOBARCHY_AGENT_MAX_ITERATIONS", "40")),
            source=source,
        )
