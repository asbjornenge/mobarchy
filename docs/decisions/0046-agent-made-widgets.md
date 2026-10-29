# 0046: Home widgets made by the agent: a description, not code

- Status: accepted (2026-10-29)
- Date: 2026-10-29

## Context

The home board's widgets were the shell's own cards (ADR 0044). The owner wanted to have
Frigg make new ones by asking: "make a news widget that only shows on the big screen".

Two ways were weighed:
- **The agent writes QML** that the shell loads. This allows anything, but code written
  by a language model would run inside the shell with all of its rights. A web page the
  agent read on the way could steer it (prompt injection), and a broken widget could take
  the home screen down.
- **The agent writes a description** that the shell draws with templates of its own.
  This allows less, but nothing the agent writes runs anywhere.

The owner chose the description, to start with.

## Decision

- **A widget is a description in `home.json`'s `"widgets"`**, `{id: spec}`:
  - a title and an icon;
  - a **template**: `list` (items: a title, a line, an image, a link), `value` (a value
    and a caption), `text` (markdown) or `image`;
  - a **source**:
    - `feed` (RSS/Atom);
    - `json` (dotted paths into any JSON);
    - `image` (an image's URL);
    - `static` (the content itself);
  - how often it is fetched (5 minutes at the least);
  - its place on the board: a size and the screens it shows on.
- **`mobarchy-widgets`** (in `mobarchy-mcp`) fetches each source when it is due and writes
  the content to `~/.local/state/mobarchy/widgets/<id>.json`. It runs from a user timer
  every 5 minutes, and at once when the agent makes or changes a widget.
  - The images are fetched there too and kept beside the content. The shell shows files
    and fetches nothing for a widget.
  - Links are http(s) only.
  - A failed fetch keeps what was shown and records the error.
- **The shell** (`WidgetCard.qml`, `WidgetContents`) draws each widget with its template:
  a card of kind `widget:<id>` on the board.
  - It is moved, resized and removed like the shell's own cards.
  - Markdown is shown without HTML or images, and a tap opens only a web link.
- **The board's entries take `"screens"`:** `all`, `wide` (shown only when the board is 8
  cells across: a fold's inner screen, a tablet) or `narrow`. This holds for any card,
  Mobarchy's own too.
- **MCP tools:**
  - `widget_create`, `widget_update`, `widget_remove`, `widget_list`, `widget_refresh`;
  - `home_board_set`, for the order, sizes and screens of every card.
- While the owner hasn't arranged the board, it is Mobarchy's default with the widgets
  after it. The default is listed twice, in `HomeBoard.qml` and in
  `mobarchy_mcp.widgets.DEFAULT_BOARD`; keep the two alike.

## Consequences

- A widget is limited to what the templates draw; more templates (or sources) come as
  they are wanted.
- **Not yet: a source that is a task for the agent** (e.g. "summarise today's news"). It
  needs a run of the agent without the phone's tools (the web only, unattended) and costs
  tokens on each refresh.
- Writing QML stays open as a later step, if the templates turn out too narrow. It would
  need a sandbox of its own (another process) first.
