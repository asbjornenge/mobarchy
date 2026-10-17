"""Calls and SMS through oFono on the system bus (ADR 0031), with busctl.

The text messages are the shell's file, ~/.local/share/mobarchy/messages.json ({number:
[{text, incoming, time}]}, one thread per person however the number is written): the shell
keeps the incoming ones, and what is sent from here is added the same way, so the Messages
app shows it too."""

from __future__ import annotations

import json
import os
import re
import subprocess
import time
from pathlib import Path

from . import contacts

OFONO = "org.ofono"


class PhoneError(Exception):
    pass


def _busctl(*args: str, timeout: float = 15) -> dict | None:
    r = subprocess.run(["busctl", "--system", "--json=short", "call", OFONO, *args],
                       capture_output=True, text=True, timeout=timeout)
    if r.returncode != 0:
        raise PhoneError((r.stderr.strip() or "oFono did not answer").removeprefix("Call failed: "))
    return json.loads(r.stdout) if r.stdout.strip() else None


def _props(data: dict) -> dict:
    """busctl's {"name": {"type", "data"}} -> {"name": data}."""
    return {k: v.get("data") for k, v in data.items()}


def modem() -> str:
    """The modem in use: the one that is online with a SIM (the first, otherwise)."""
    modems = _busctl("/", "org.ofono.Manager", "GetModems")["data"][0]
    if not modems:
        raise PhoneError("no modem (oFono has none)")
    for path, props in modems:
        if _props(props).get("Online"):
            return path
    return modems[0][0]


def status() -> dict:
    m = modem()
    out: dict = {"modem": m}
    try:
        sim = _props(_busctl(m, "org.ofono.SimManager", "GetProperties")["data"][0])
        out["sim"] = {"present": sim.get("Present"), "pin_required": sim.get("PinRequired"),
                      "operator": sim.get("ServiceProviderName", "")}
    except PhoneError:
        out["sim"] = None
    try:
        net = _props(_busctl(m, "org.ofono.NetworkRegistration", "GetProperties")["data"][0])
        out["network"] = {"status": net.get("Status"), "operator": net.get("Name"),
                          "technology": net.get("Technology"), "signal_percent": net.get("Strength")}
    except PhoneError:
        out["network"] = None
    out["calls"] = calls(m)
    return out


def calls(m: str | None = None) -> list[dict]:
    m = m or modem()
    out = []
    for path, props in _busctl(m, "org.ofono.VoiceCallManager", "GetCalls")["data"][0]:
        p = _props(props)
        number = p.get("LineIdentification", "")
        out.append({"call": path, "state": p.get("State"), "number": number,
                    "name": contacts.name_for(number) or p.get("Name", "")})
    return out


def normalize(number: str) -> str:
    n = re.sub(r"[^\d+*#]", "", number)
    if not re.search(r"\d", n):
        raise PhoneError(f"{number!r} is not a phone number")
    return n


def resolve(who: str) -> tuple[str, str]:
    """A number, or a contact (name, nickname or the owner's word for them: "far") ->
    (number, name). Several numbers: the mobile one."""
    if re.fullmatch(r"[\d\s+()\-*#]+", who.strip()):
        n = normalize(who)
        return n, contacts.name_for(n)
    found = contacts.search(who)
    if not found:
        raise PhoneError(f"no contact matches {who!r}: ask the owner who that is (a number, or "
                         f"which contact), and remember it with contacts_set_relation")
    best = found[0]
    if len(found) > 1 and contacts.fold_text(who) not in (
            [contacts.fold_text(best["name"])] + [contacts.fold_text(r) for r in best.get("relations", [])]
            + [contacts.fold_text(n) for n in best.get("nicknames", [])]):
        raise PhoneError(f"{who!r} matches several contacts: "
                         + ", ".join(c["name"] for c in found[:8]) + " — ask which one")
    phones = best["phones"]
    if not phones:
        raise PhoneError(f"{best['name']} has no phone number")
    mobile = next((p for p in phones if p["type"] in ("cell", "mobile", "iphone")), phones[0])
    return normalize(mobile["number"]), best["name"]


def dial(who: str) -> dict:
    number, name = resolve(who)
    if any(c["state"] in ("active", "dialing", "alerting") for c in calls()):
        raise PhoneError("a call is already going: hang up first")
    path = _busctl(modem(), "org.ofono.VoiceCallManager", "Dial", "ss", number, "")["data"][0]
    return {"calling": name or number, "number": number, "call": path}


def hangup() -> dict:
    ongoing = calls()
    if not ongoing:
        return {"hung_up": 0}
    _busctl(modem(), "org.ofono.VoiceCallManager", "HangupAll")
    return {"hung_up": len(ongoing)}


# --- SMS ------------------------------------------------------------------------------------

def messages_path() -> Path:
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "mobarchy/messages.json"


def _load() -> dict:
    try:
        return json.loads(messages_path().read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def _store(number: str, text: str, incoming: bool) -> None:
    data = _load()
    key = next((k for k in data if contacts.same_number(k, number)), number)
    thread = data.pop(key, []) + [{"text": text, "incoming": incoming, "time": int(time.time())}]
    data[number if len(contacts.digits(number)) > len(contacts.digits(key)) else key] = thread
    p = messages_path()
    p.parent.mkdir(parents=True, exist_ok=True)
    tmp = p.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(data, ensure_ascii=False))
    os.replace(tmp, p)


def send_sms(who: str, text: str) -> dict:
    if not text.strip():
        raise PhoneError("the message is empty")
    number, name = resolve(who)
    _busctl(modem(), "org.ofono.MessageManager", "SendMessage", "ss", number, text, timeout=30)
    _store(number, text, incoming=False)
    return {"sent_to": name or number, "number": number}


def list_sms(who: str = "", limit: int = 20) -> dict:
    """The newest messages, of one person (a name or number) or of everyone."""
    data = _load()
    if who.strip():
        try:
            number, _ = resolve(who)
        except PhoneError:
            number = who
        data = {k: v for k, v in data.items() if contacts.same_number(k, number)}
    cards = contacts.load()
    out = []
    for number, thread in data.items():
        name = contacts.name_for(number, cards)
        for m in thread:
            out.append({"from" if m["incoming"] else "to": name or number, "number": number,
                        "text": m["text"],
                        "time": time.strftime("%Y-%m-%d %H:%M", time.localtime(m.get("time", 0)))})
    out.sort(key=lambda m: m["time"], reverse=True)
    return {"messages": out[:max(1, min(limit, 200))]}
