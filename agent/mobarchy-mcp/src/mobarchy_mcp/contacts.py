"""The owner's contacts (ADR 0031, 0032): vCard files, the format every phone and iCloud exports,
that the shell (Phone, Messages) and the agent both read:

  ~/.local/share/mobarchy/contacts.vcf        the owner's own (written here)
  ~/.local/share/mobarchy/contacts/<id>.vcf   each internet account's (written by accounts.py)
  ~/.local/share/mobarchy/relations.json      the owner's word for people: {"far": "Per Olsen"}

Reading takes them all, one person per name (numbers from every source together).

A card is kept as its own lines, so what this module does not understand (photos, Apple's
item groups, notes) survives a rewrite untouched. It reads FN/N, TEL, EMAIL, NICKNAME, ORG,
and the owner's relation to the person, X-MOBARCHY-RELATION ("far", "mamma", "sjef"): how
the owner refers to them, so "ring min far" finds the right card. A relation is the owner's
word, set when they say who someone is.

Writes replace the file whole (rename) under a lock (contacts.vcf.lock), as alarms.json."""

from __future__ import annotations

import contextlib
import fcntl
import json
import os
import re
import unicodedata
from dataclasses import dataclass, field
from pathlib import Path

RELATION = "X-MOBARCHY-RELATION"


class ContactError(Exception):
    pass


def _data() -> Path:
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "mobarchy"


def path() -> Path:
    return _data() / "contacts.vcf"


def relations_path() -> Path:
    return _data() / "relations.json"


def load_relations() -> dict[str, str]:
    try:
        return json.loads(relations_path().read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


# --- vCard ----------------------------------------------------------------------------------

def unfold(text: str) -> list[str]:
    """Physical lines -> logical lines (RFC 6350 folding: a line starting with a space or tab
    continues the previous one)."""
    out: list[str] = []
    for raw in text.replace("\r\n", "\n").replace("\r", "\n").split("\n"):
        if raw[:1] in (" ", "\t") and out:
            out[-1] += raw[1:]
        elif raw:
            out.append(raw)
    return out


def unescape(value: str) -> str:
    return re.sub(r"\\(.)", lambda m: "\n" if m.group(1) in "nN" else m.group(1), value)


def escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace("\n", "\\n").replace(",", "\\,").replace(";", "\\;")


def split_line(line: str) -> tuple[str, dict[str, str], str]:
    """"item1.TEL;TYPE=CELL,VOICE:+47 ..." -> ("TEL", {"TYPE": "CELL,VOICE"}, "+47 ...")."""
    head, _, value = line.partition(":")
    parts = head.split(";")
    name = parts[0].split(".")[-1].upper()  # Apple's "item1." groups
    params: dict[str, str] = {}
    for p in parts[1:]:
        k, _, v = p.partition("=")
        key, v = (k.upper(), v.strip('"')) if v else ("TYPE", k)  # vCard 2.1: ;CELL alone
        # repeated (iCloud: type=CELL;type=VOICE;type=pref): one comma list
        params[key] = ",".join(x for x in (params.get(key, ""), v) if x)
    return name, params, value


@dataclass
class Card:
    lines: list[str] = field(default_factory=list)  # BEGIN..END, unfolded
    # the owner's words for this person, from relations.json (load())
    words: list[str] = field(default_factory=list)

    def values(self, prop: str) -> list[tuple[dict[str, str], str]]:
        out = []
        for line in self.lines:
            name, params, value = split_line(line)
            if name == prop:
                out.append((params, value))
        return out

    def first(self, prop: str) -> str:
        v = self.values(prop)
        return unescape(v[0][1]) if v else ""

    @property
    def name(self) -> str:
        fn = self.first("FN").strip()
        if fn:
            return fn
        n = self.first("N").split(";")  # family;given;middle;prefix;suffix
        return " ".join(x for x in (n[1:2] + n[2:3] + n[:1]) if x).strip()

    @property
    def phones(self) -> list[dict]:
        out = []
        for params, value in self.values("TEL"):
            number = unescape(value).removeprefix("tel:").strip()
            kinds = [t.lower() for t in params.get("TYPE", "").split(",") if t and t.lower() not in ("voice", "pref")]
            out.append({"number": number, "type": kinds[0] if kinds else ""})
        return out

    @property
    def emails(self) -> list[str]:
        return [unescape(v).strip() for _, v in self.values("EMAIL")]

    @property
    def relations(self) -> list[str]:
        own = [unescape(v).strip() for _, v in self.values(RELATION) if v.strip()]
        return own + [w for w in self.words if w not in own]

    @property
    def nicknames(self) -> list[str]:
        return [n.strip() for _, v in self.values("NICKNAME") for n in unescape(v).split(",") if n.strip()]

    def summary(self) -> dict:
        d = {"name": self.name, "phones": self.phones}
        for key, value in (("emails", self.emails), ("relations", self.relations),
                           ("nicknames", self.nicknames), ("organization", self.first("ORG").replace(";", " ").strip())):
            if value:
                d[key] = value
        return d

    def add_line(self, line: str) -> None:
        self.lines.insert(len(self.lines) - 1, line)  # before END:VCARD

    def remove(self, prop: str, value: str | None = None) -> None:
        self.lines = [l for l in self.lines
                      if not (split_line(l)[0] == prop and (value is None or unescape(split_line(l)[2]).strip().lower() == value.lower()))]


def parse(text: str) -> list[Card]:
    cards, current = [], None
    for line in unfold(text):
        upper = line.upper()
        if upper == "BEGIN:VCARD":
            current = Card([line])
        elif current is not None:
            current.lines.append(line)
            if upper == "END:VCARD":
                cards.append(current)
                current = None
    return cards


def fold(line: str) -> str:
    """Lines longer than 75 octets are folded (RFC 6350)."""
    data, out = line.encode(), []
    while len(data) > 75:
        cut = 75 if not out else 74
        while cut > 0 and (data[cut] & 0xC0) == 0x80:  # not inside a UTF-8 character
            cut -= 1
        out.append(data[:cut].decode())
        data = data[cut:]
    out.append(data.decode())
    return "\r\n ".join(out)


def serialize(cards: list[Card]) -> str:
    return "".join("\r\n".join(fold(l) for l in c.lines) + "\r\n" for c in cards)


# --- the file -------------------------------------------------------------------------------

def load_local() -> list[Card]:
    try:
        return parse(path().read_text(encoding="utf-8", errors="replace"))
    except FileNotFoundError:
        return []


def load() -> list[Card]:
    """Everyone: the owner's own cards and the accounts', one card per name (a person in two
    places gets the numbers of both), with the owner's words for them."""
    cards = load_local()
    by_name = {fold_text(c.name): c for c in cards if c.name}
    for f in sorted((_data() / "contacts").glob("*.vcf")):
        for c in parse(f.read_text(encoding="utf-8", errors="replace")):
            if not c.name:
                continue
            have = by_name.get(fold_text(c.name))
            if have is None:
                cards.append(c)
                by_name[fold_text(c.name)] = c
                continue
            have.lines = list(have.lines)  # merged only in memory
            for line in c.lines:
                if split_line(line)[0] == "TEL" and not any(
                        same_number(unescape(split_line(line)[2]), p["number"]) for p in have.phones):
                    have.add_line(line)
    for word, name in load_relations().items():
        c = by_name.get(fold_text(name))
        if c is not None:
            c.words.append(word)
    return cards


@contextlib.contextmanager
def editing():
    p = path()
    p.parent.mkdir(parents=True, exist_ok=True)
    with open(f"{p}.lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        cards = load_local()
        yield cards
        tmp = p.with_suffix(".vcf.tmp")
        tmp.write_text(serialize(cards), encoding="utf-8")
        os.replace(tmp, p)


# --- finding --------------------------------------------------------------------------------

def fold_text(s: str) -> str:
    """For matching: lower case, accents off (but æ ø å kept: they are letters here)."""
    s = s.lower().replace("æ", "\0ae").replace("ø", "\0oe").replace("å", "\0aa")
    s = "".join(c for c in unicodedata.normalize("NFKD", s) if not unicodedata.combining(c))
    return s.replace("\0ae", "æ").replace("\0oe", "ø").replace("\0aa", "å")


def digits(number: str) -> str:
    return re.sub(r"\D", "", number)


def same_number(a: str, b: str) -> bool:
    """The same phone, however written (+4791234567, 91234567, 912 34 567): the last 8
    digits."""
    da, db = digits(a), digits(b)
    return len(da) >= 6 and len(db) >= 6 and da[-8:] == db[-8:]


def search(query: str, cards: list[Card] | None = None) -> list[dict]:
    """Cards matching a name, part of a name, a nickname, a relation ("far") or a number,
    best first: a relation or whole name ranks above a word, a word above part of one."""
    cards = load() if cards is None else cards
    q = fold_text(query.strip())
    if not q:
        return []
    scored = []
    for c in cards:
        if any(digits(q) and same_number(q, p["number"]) for p in c.phones):
            scored.append((0, c))
            continue
        name = fold_text(c.name)
        words = name.split()
        relations = [fold_text(r) for r in c.relations]
        nicks = [fold_text(n) for n in c.nicknames]
        if q in relations or q == name or q in nicks:
            score = 0
        elif q in words:
            score = 1
        elif any(w.startswith(q) for w in words) or any(r.startswith(q) for r in relations):
            score = 2
        elif q in name:
            score = 3
        else:
            continue
        scored.append((score, c))
    scored.sort(key=lambda sc: (sc[0], sc[1].name.lower()))
    return [c.summary() for _, c in scored]


def name_for(number: str, cards: list[Card] | None = None) -> str:
    cards = load() if cards is None else cards
    for c in cards:
        if any(same_number(number, p["number"]) for p in c.phones):
            return c.name
    return ""


# --- changing -------------------------------------------------------------------------------

def _find(cards: list[Card], name: str) -> Card:
    exact = [c for c in cards if fold_text(c.name) == fold_text(name)]
    if len(exact) == 1:
        return exact[0]
    matches = [c for c in cards if fold_text(name) in fold_text(c.name)]
    if len(matches) == 1:
        return matches[0]
    if not matches:
        raise ContactError(f"no contact called {name!r}")
    raise ContactError(f"{name!r} matches {len(matches)} contacts: "
                       + ", ".join(c.name for c in matches[:8]) + " — use the full name")


def add(name: str, phones: list[str], email: str = "", relation: str = "") -> dict:
    name = name.strip()
    if not name:
        raise ContactError("a contact needs a name")
    if not phones and not email:
        raise ContactError("give a phone number or an e-mail address")
    with editing() as cards:
        if any(fold_text(c.name) == fold_text(name) for c in cards):
            raise ContactError(f"there is already a contact called {name!r}: change that one")
        lines = ["BEGIN:VCARD", "VERSION:3.0", f"FN:{escape(name)}"]
        parts = name.rsplit(" ", 1)
        lines.append(f"N:{escape(parts[-1]) if len(parts) > 1 else ''};{escape(parts[0])};;;")
        lines += [f"TEL;TYPE=CELL:{p.strip()}" for p in phones if p.strip()]
        if email:
            lines.append(f"EMAIL;TYPE=INTERNET:{email.strip()}")
        if relation:
            lines.append(f"{RELATION}:{escape(relation.strip())}")
        lines.append("END:VCARD")
        card = Card(lines)
        cards.append(card)
        return card.summary()


def set_relation(name: str, relation: str) -> dict:
    """The owner's word for this person ("far"), for anyone in the contacts (an account's too);
    one person per word: it moves from anyone who had it."""
    relation = relation.strip()
    if not relation:
        raise ContactError("say what the person is to the owner (e.g. far, mamma, sjef)")
    card = _find(load(), name)
    with editing() as local:  # the lock; and an old X-MOBARCHY-RELATION with this word goes
        for c in local:
            c.remove(RELATION, relation)
        words = {w: n for w, n in load_relations().items() if fold_text(w) != fold_text(relation)}
        words[relation] = card.name
        relations_path().write_text(json.dumps(words, ensure_ascii=False, indent=1))
    return next(c.summary() for c in load() if c.name == card.name)


def add_phone(name: str, number: str, kind: str = "cell") -> dict:
    """A number for someone: on the owner's own card; for an account's contact, on an own card
    with the same name (shown together; the account is never written)."""
    person = _find(load(), name)
    if any(same_number(number, p["number"]) for p in person.phones):
        return person.summary()
    with editing() as cards:
        own = next((c for c in cards if fold_text(c.name) == fold_text(person.name)), None)
        if own is None:
            own = Card(["BEGIN:VCARD", "VERSION:3.0", f"FN:{escape(person.name)}", "END:VCARD"])
            cards.append(own)
        own.add_line(f"TEL;TYPE={kind.upper()}:{number.strip()}")
    return next(c.summary() for c in load() if c.name == person.name)


def delete(name: str) -> dict:
    """The owner's own card (an account's contacts are the account's: delete them there)."""
    with editing() as cards:
        try:
            card = _find(cards, name)
        except ContactError:
            if any(fold_text(c.name) == fold_text(name) for c in load()):
                raise ContactError(f"{name} comes from an internet account (iCloud, Google): "
                                   "delete them there") from None
            raise
        cards.remove(card)
        return {"deleted": card.name}


def import_vcards(text: str) -> dict:
    """Merge a vCard export (from iCloud, another phone): new people are added; someone
    already here (same name) gets the numbers they lack."""
    new = parse(text)
    if not new:
        raise ContactError("no vCards in that file")
    added = merged = 0
    with editing() as cards:
        by_name = {fold_text(c.name): c for c in cards}
        for c in new:
            if not c.name:
                continue
            have = by_name.get(fold_text(c.name))
            if have is None:
                cards.append(c)
                by_name[fold_text(c.name)] = c
                added += 1
                continue
            for line in c.lines:
                if split_line(line)[0] == "TEL" and not any(
                        same_number(unescape(split_line(line)[2]), p["number"]) for p in have.phones):
                    have.add_line(line)
                    merged += 1
    return {"added": added, "numbers_merged": merged, "total": len(load_local())}
