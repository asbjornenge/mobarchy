"""The app store (ADR 0012): Mobarchy's curated catalog, with AppStream's metadata for
packages, installing through PackageKit (pkgcli; polkit lets the owner) and web apps as
desktop entries. The shell's Store app and the agent's tools both use it.

    mobarchy-store list [--json]         the catalog, with what is installed
    mobarchy-store install|remove ID     (JSON on stdout, errors as {"error": ...})
"""

from __future__ import annotations

import argparse
import gzip
import json
import os
import subprocess
import sys
import tomllib
import xml.etree.ElementTree as ET
from pathlib import Path

from . import webapps

CATALOG = Path(os.environ.get("MOBARCHY_STORE_CATALOG", "/usr/share/mobarchy/store/catalog.toml"))
APPSTREAM = Path("/usr/share/swcatalog")
CACHE = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "mobarchy/store"
LANG_ATTR = "{http://www.w3.org/XML/1998/namespace}lang"
CATEGORIES = {"music": "Music & audio", "video": "Video", "communication": "Communication",
              "internet": "Internet", "reading": "Reading & news", "photos": "Photos",
              "maps": "Maps & weather", "productivity": "Productivity", "utilities": "Utilities"}


def load_catalog(path: Path = CATALOG) -> list[dict]:
    return tomllib.loads(path.read_text())["app"]


# --- AppStream ----------------------------------------------------------------------------

def _default(elem: ET.Element, tag: str) -> ET.Element | None:
    """The untranslated child (AppStream lists every translation as a sibling)."""
    return next((e for e in elem.findall(tag) if LANG_ATTR not in e.attrib), None)


def _text(elem: ET.Element | None) -> str:
    return " ".join("".join(elem.itertext()).split()) if elem is not None else ""


def parse_component(c: ET.Element) -> dict:
    desc = _default(c, "description")
    paragraphs = []
    if desc is not None:
        for e in desc:
            if LANG_ATTR in e.attrib:
                continue
            if e.tag == "p":
                paragraphs.append(_text(e))
            elif e.tag in ("ul", "ol"):
                paragraphs.append("\n".join("• " + _text(li) for li in e if LANG_ATTR not in li.attrib))
    icons = {int(i.get("width", 0)): i.text for i in c.findall("icon") if i.get("type") == "cached"}
    shots = []
    for s in c.findall("screenshots/screenshot"):
        images = [i for i in s.findall("image") if LANG_ATTR not in i.attrib]
        source = next((i for i in images if i.get("type") == "source"), images[0] if images else None)
        if source is not None and source.text:
            shots.append(source.text.strip())
    launch = c.find("launchable[@type='desktop-id']")
    return {"appstream_id": c.findtext("id"), "name": _text(_default(c, "name")),
            "summary": _text(_default(c, "summary")), "description": "\n\n".join(paragraphs),
            "icon_file": icons[max(icons)] if icons else None, "icon_size": max(icons) if icons else 0,
            "screenshots": shots[:4], "desktop_id": launch.text.removesuffix(".desktop") if launch is not None else None}


def appstream_index(wanted: set[str]) -> dict[str, dict]:
    """AppStream data for the wanted packages, by package name (cached: parsing takes a while)."""
    sources = sorted(APPSTREAM.glob("xml/*.xml.gz"))
    stamp = [f"{p}:{p.stat().st_mtime}" for p in sources]
    cache = CACHE / "appstream.json"
    try:
        data = json.loads(cache.read_text())
        if data["stamp"] == stamp and wanted <= set(data["packages"]) | set(data["missing"]):
            return data["packages"]
    except (OSError, ValueError, KeyError):
        pass
    found: dict[str, dict] = {}
    for src in sources:
        repo = src.name.removesuffix(".xml.gz")
        for _, c in ET.iterparse(gzip.open(src), events=("end",)):
            if c.tag != "component":
                continue
            pkg = c.findtext("pkgname")
            if pkg in wanted and pkg not in found and c.get("type") in ("desktop-application", "desktop", None):
                info = parse_component(c)
                info["icon_dir"] = f"icons/archlinux-arch-{repo}/{info['icon_size']}x{info['icon_size']}"
                found[pkg] = info
            c.clear()
    CACHE.mkdir(parents=True, exist_ok=True)
    cache.write_text(json.dumps({"stamp": stamp, "packages": found, "missing": sorted(wanted - set(found))}))
    return found


def icon_png(pkg: str, info: dict) -> str | None:
    """AppStream's cached icons are JPEG XL, which Qt doesn't read: convert once to PNG."""
    if not info.get("icon_file"):
        return None
    src = APPSTREAM / info["icon_dir"] / info["icon_file"]
    out = CACHE / "icons" / f"{pkg}.png"
    if out.exists() and src.exists() and out.stat().st_mtime >= src.stat().st_mtime:
        return str(out)
    if not src.exists():
        return None
    out.parent.mkdir(parents=True, exist_ok=True)
    if src.suffix == ".png":
        out.write_bytes(src.read_bytes())
        return str(out)
    r = subprocess.run(["djxl", str(src), str(out)], capture_output=True)
    return str(out) if r.returncode == 0 else None


def webapp_icon(app: dict) -> str | None:
    """The site's own icon (its manifest's largest), fetched once and kept in the cache."""
    out = CACHE / "icons" / f"web-{app['id']}"
    for f in out.parent.glob(f"web-{app['id']}.*") if out.parent.exists() else []:
        return str(f)
    try:
        url = webapps.describe(app["url"])["icon_url"]
        data, _ = webapps._get(url)
    except (OSError, ValueError, KeyError):
        return None
    ext = Path(url.split("?")[0]).suffix.lower()
    out = out.with_suffix(ext if ext in (".png", ".ico", ".jpg", ".jpeg", ".webp") else ".png")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    return str(out)


# --- what is installed --------------------------------------------------------------------

def installed_packages() -> set[str]:
    r = subprocess.run(["pacman", "-Qq"], capture_output=True, text=True)
    return set(r.stdout.split())


def webapp_key(app: dict) -> str:
    return webapps.PREFIX + webapps.slug(app["name"])


def installed_webapp(app: dict, existing: list[dict]) -> str | None:
    """The desktop ID of this web app if it is on the home screen: added from the store,
    or installed from Chromium under the same name."""
    for w in existing:
        if w["app_id"] == webapp_key(app) or w["name"].lower() == app["name"].lower():
            return w["app_id"]
    return None


def entries(catalog: list[dict] | None = None) -> list[dict]:
    catalog = catalog if catalog is not None else load_catalog()
    packages = {a["package"] for a in catalog if a["kind"] == "package"}
    meta = appstream_index(packages)
    have = installed_packages()
    existing = webapps.listed()
    out = []
    for a in catalog:
        e = {"id": a["id"], "kind": a["kind"], "category": a.get("category", "utilities"),
             "category_name": CATEGORIES.get(a.get("category", ""), a.get("category", "")),
             "tested": a.get("tested", []), "note": a.get("note", "")}
        if a["kind"] == "package":
            m = meta.get(a["package"], {})
            e.update(package=a["package"], name=a.get("name") or m.get("name") or a["package"],
                     summary=a.get("summary") or m.get("summary", ""), description=m.get("description", ""),
                     icon=a.get("icon") or icon_png(a["package"], m) or "", screenshots=m.get("screenshots", []),
                     installed=a["package"] in have, app_id=m.get("desktop_id") or a["package"])
        else:
            app_id = installed_webapp(a, existing)
            e.update(url=a["url"], name=a["name"], summary=a.get("summary", ""), description=a.get("description", ""),
                     icon=a.get("icon") or webapp_icon(a) or "", screenshots=[], installed=app_id is not None,
                     app_id=app_id or webapp_key(a))
        out.append(e)
    return out


def find(app_id: str) -> dict:
    for a in load_catalog():
        if a["id"] == app_id:
            return a
    raise ValueError(f"no app {app_id!r} in the store")


def install(app_id: str) -> dict:
    a = find(app_id)
    if a["kind"] == "package":
        r = subprocess.run(["pkgcli", "-y", "install", a["package"]], capture_output=True, text=True, timeout=900)
        if r.returncode != 0:
            raise RuntimeError((r.stderr or r.stdout).strip().splitlines()[-1] if (r.stderr or r.stdout).strip() else "pkgcli failed")
    else:
        result = webapps.add(a["url"], a["name"], keep_url=True)
        return {"id": app_id, "installed": True, "app_id": result["app_id"]}
    return {"id": app_id, "installed": True}


def remove(app_id: str) -> dict:
    a = find(app_id)
    if a["kind"] == "package":
        r = subprocess.run(["pkgcli", "-y", "remove", a["package"]], capture_output=True, text=True, timeout=600)
        if r.returncode != 0:
            raise RuntimeError((r.stderr or r.stdout).strip().splitlines()[-1] if (r.stderr or r.stdout).strip() else "pkgcli failed")
    else:
        key = installed_webapp(a, webapps.listed())
        if key:
            webapps.remove(key)
    return {"id": app_id, "installed": False}


def main() -> None:
    p = argparse.ArgumentParser(prog="mobarchy-store")
    sub = p.add_subparsers(dest="cmd", required=True)
    ls = sub.add_parser("list")
    ls.add_argument("--json", action="store_true")
    for name in ("install", "remove"):
        sub.add_parser(name).add_argument("id")
    args = p.parse_args()
    try:
        if args.cmd == "list":
            items = entries()
            if args.json:
                print(json.dumps({"categories": CATEGORIES, "apps": items}, ensure_ascii=False))
            else:
                for e in items:
                    print(f"{'✓' if e['installed'] else ' '} {e['id']:<20} {e['name']} — {e['summary']}")
        elif args.cmd == "install":
            print(json.dumps(install(args.id)))
        else:
            print(json.dumps(remove(args.id)))
    except (ValueError, RuntimeError, OSError, subprocess.TimeoutExpired) as e:
        print(json.dumps({"error": str(e)}))
        sys.exit(1)
