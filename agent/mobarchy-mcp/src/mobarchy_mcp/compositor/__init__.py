"""Compositor adapters (ADR 0004).

Tools talk to a Compositor, never to a specific compositor's IPC. Adding a
compositor (Phoc on Halium, a purpose-built one...) means adding an adapter.
"""

from __future__ import annotations

import os
from abc import ABC, abstractmethod
from dataclasses import asdict, dataclass


@dataclass
class Window:
    id: str
    app_id: str
    title: str
    focused: bool
    x: int          # logical pixels
    y: int
    width: int
    height: int
    output: str

    def to_dict(self) -> dict:
        return asdict(self)


@dataclass
class Output:
    name: str
    width: int      # physical pixels
    height: int
    scale: float
    x: int = 0      # logical position in the global layout
    y: int = 0

    @property
    def logical_width(self) -> int:
        return round(self.width / self.scale)

    @property
    def logical_height(self) -> int:
        return round(self.height / self.scale)

    def to_dict(self) -> dict:
        return {**asdict(self), "logical_width": self.logical_width, "logical_height": self.logical_height}


class Compositor(ABC):
    name: str

    @abstractmethod
    def outputs(self) -> list[Output]: ...

    @abstractmethod
    def windows(self) -> list[Window]: ...

    @abstractmethod
    def focus(self, window_id: str) -> None: ...

    @abstractmethod
    def close(self, window_id: str) -> None: ...

    @abstractmethod
    def launch(self, command: str) -> None: ...


class CompositorError(Exception):
    pass


def detect() -> Compositor:
    if os.environ.get("HYPRLAND_INSTANCE_SIGNATURE"):
        from .hyprland import Hyprland
        return Hyprland()
    raise CompositorError("no supported compositor found (is the Wayland session environment set?)")
