# 0012: A curated app store on top of Arch (and the web)

- Status: accepted
- Date: 2026-10-03

## Context

Everything in Arch Linux ARM installs on Mobarchy, but most desktop apps don't work on a
phone (fixed window sizes, mouse-only UI). Users should find what works without testing
it themselves, and still be free to install anything. Some of the best "apps" on a phone
are websites (web apps, ADR 0011 for DRM).

## Decision

- **Catalog**: `os/store/catalog.toml`, a curated list of apps known to work on Mobarchy,
  each tagged with the devices it was tested on. An entry is a *package* (from Arch or
  the Mobarchy repository) or a *web app* (a site opened in its own window, Chromium's
  app mode, with name and icon from its manifest). It ships as the package
  `mobarchy-store-catalog`, so it updates with the system.
- **Metadata** for packages comes from AppStream (`archlinux-appstream-data`): name,
  summary, description, icon, screenshots. The catalog adds only what AppStream can't
  say: the category, notes, where it was tested.
- **Installing** goes through PackageKit (the standard D-Bus service, alpm backend), via
  `pkcon`; a polkit rule lets the owner (group wheel) install and remove without a
  password prompt. Web apps are desktop entries in the user's applications directory.
- **One backend, two faces**: `mobarchy-store` (in mobarchy-mcp) lists, searches,
  installs and removes, with JSON output. The shell's Store app (`mobarchy-shell
  --store`) and the agent's MCP tools both use it.
- The system itself (shell, agent, ...) is never in the store: it comes with the image
  and updates with the system (ADR 0011's repository, `pacman -Syu`).

## Consequences

- A curated catalog is maintenance: entries need re-testing as apps change.
- Anything else stays installable (pacman, or the agent).
- The catalog format is small on purpose; ratings, reviews or third-party sources would
  be a later decision.
