# 0025: Themes are files the agent can make

- Status: accepted
- Date: 2026-10-06

## Context

Mobarchy's look is Tokyo Night (Omarchy's default): ten colour roles and a font, in
`Theme.qml` singletons in the shell and in the browser. The owner wants this to stay
Mobarchy's own theme. They also want to ask the agent for a new one ("a Pokémon theme",
"anything") and see the phone change.

All of the shell's and the browser's colours already go through `Theme` (bg, bgDark,
panel, line, fg, muted, accent, green, red, magenta, plus the font). Only translucent dims
are written out, and they stay.

## Decision

- **A theme is a JSON file:** `{"name", "description", "colors": {the ten roles},
  "font"}`.
  - Mobarchy's own come with the shell, in `/usr/share/mobarchy/themes/`. `mobarchy.json`
    is Tokyo Night, the default.
  - The owner's themes, the agent's included, go in `~/.local/share/mobarchy/themes/` and
    win over the system's of the same id.
  - The active theme's id is in `~/.config/mobarchy/theme`.
- **Followed live.** `ThemeSource` (C++; the shell's, with a copy in the browser) reads the
  active theme, fills what it leaves out from Mobarchy's own, and watches the files. The
  `Theme` singletons bind to it. A switch recolours the shell (with Store and Camera) and
  the browser at once, without a restart.
- **MCP tools** (`themes.py`):
  - `theme_list`, `theme_current` (with what each role is for), and `theme_set`
  - `theme_create`, which writes the theme and switches to it
  - `theme_delete`, which removes one of the owner's themes; Mobarchy's own stay
  - All of them are `act` level: reversible.
- **A theme has to stay readable.** `theme_create` checks contrast (WCAG ratios):
  - text 4.5 on bg and on panel;
  - muted 2.5;
  - accent, red and green 3 on bg;
  - the buttons' text (bg) 3 on the accent.

  A font has to be installed. A failed check names the pair to fix, so the agent can try
  again.

## Consequences

- Asking the agent for a theme changes the phone's look in seconds, and any theme can be
  undone (`theme_set mobarchy`).
- Web pages, other apps (foot, GTK and Qt apps) and the app icons keep their own colours.
  Theming them would be a step further.
- Two copies of `ThemeSource` (the shell and the browser are separate programs) must be kept
  the same.
