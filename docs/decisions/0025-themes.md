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
  "font", "wallpaper"}`. The wallpaper is an image next to the theme file, named in it.
  - Mobarchy's own come with the shell, in `/usr/share/mobarchy/themes/`. `mobarchy.json`
    is Tokyo Night, the default.
  - The owner's themes, the agent's included, go in `~/.local/share/mobarchy/themes/` and
    win over the system's of the same id.
  - The active theme's id is in `~/.config/mobarchy/theme`.
- **Followed live.** `ThemeSource` (C++; the shell's, with a copy in the browser) reads the
  active theme, fills what it leaves out from Mobarchy's own, and watches the files. The
  `Theme` singletons bind to it. A switch recolours the shell (with Store and Camera) and
  the browser at once, without a restart.
- **Cards.** A theme says how the home board draws its widgets: `"cards": "filled"` (the
  panel colour; the default) or `"outline"` (no fill, a thin line: airy, best without a busy
  wallpaper). `theme_create` takes it. The owner chooses it for any theme, Mobarchy's own
  too, in the Themes app or through the agent (`theme_cards`); the choice is kept beside the
  themes (`~/.config/mobarchy/theme-cards.json`), their files stay as they are.
- **Wallpaper.** The lock screen and the agent's home screen show the theme's wallpaper,
  dimmed with the theme's background so the clock and the conversation stay readable.
- **The Themes app** (`mobarchy-shell --themes`, like the Store): a card per theme in its
  own colours and wallpaper. A tap switches; holding one of the owner's offers to delete
  it. New themes come from the agent. Not quick settings, which is full enough.
- **The agent can use the web**: Claude Code's WebSearch and WebFetch, and Codex's web
  search. It can find an image that fits a style, and theme_create takes its URL.
- **MCP tools** (`themes.py`):
  - `theme_list`, `theme_current` (with what each role is for), and `theme_set`
  - `theme_create`, which writes the theme and switches to it. Its optional `wallpaper` is an
    image's URL, a file on the phone (a photo) or SVG text the agent writes; it is checked
    to be a JPEG, PNG, WebP or SVG of at most 25 MB.
  - `theme_wallpaper`, which changes the wallpaper of one of the owner's themes
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
- The agent's web access is not limited to images: it can search and read the web for any
  task, as the owner's own agents do elsewhere. The phone's tools still decide what it can
  change.
- The `api` backend (the Anthropic API directly) has no web search yet.
- Web pages, other apps (foot, GTK and Qt apps) and the app icons keep their own colours.
  Theming them would be a step further.
- Two copies of `ThemeSource` (the shell and the browser are separate programs) must be kept
  the same.
