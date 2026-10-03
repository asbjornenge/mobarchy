# 0016: Mobarchy's own on-screen keyboard, in the shell

- Status: accepted; replaces squeekboard (ADR 0006's keyboard)
- Date: 2026-10-03

## Context

squeekboard (ADR 0006) works, but the owner found it poor to type on:
- no key previews;
- a layout unlike the phones people know;
- poor Norwegian;
- no emoji.

The alternatives were:
- **Stevia** (Phosh's keyboard): GTK, built for Phosh, and doesn't look like the shell.
- **Maliit:** Qt, with little development and thin Norwegian support.

The shell is already Qt Quick (ADR 0009), in Mobarchy's theme.

## Decision

- **The shell is the session's input method.** It speaks input-method-unstable-v2 with
  the compositor:
  - When a text field gets focus (text-input-v3 in GTK, Qt, Firefox and foot), it learns
    the field's purpose and the text around the cursor.
  - It types with commit_string and deletes whole graphemes (an emoji, an accented
    letter) with delete_surrounding_text.
- **Keys that are actions go through virtual-keyboard-unstable-v1,** with a Norwegian xkb
  keymap:
  - Return, arrows and Tab.
  - BackSpace when a field doesn't share its text (terminals).
  - All typing when no field speaks text-input.
- **The keyboard is QML** (`KeyboardPanel.qml`): a top-layer surface with an exclusive
  zone, so apps make room.
  - **Layout:** an iPhone-like Norwegian layout (å, ø, æ), 123 and #+= pages, and a number
    pad for digit, phone and PIN fields. Email and URL fields get @, . and /.
  - **Touch:** key previews over the finger, and keys type on release. Every finger is
    tracked on its own, for two thumbs.
  - **Holding a key:** a letter offers its accents; space moves the cursor.
  - **Shift:** shift, caps lock on double tap, and capitals at sentence starts when the
    field asks for auto-capitalization. Two spaces after a word type ". ".
  - **Emoji:** an emoji panel from Unicode's list (tools/emoji-gen → data/emoji.json), with
    recently used emoji kept.
- **The clipboard is the shell's too** (wlr-data-control): it sees every text copy, and the
  keyboard's strip has a paste button (the latest copy; hold it for the last 20).
  - **Copying:** selecting text in an app puts it in the primary selection. The shell sees
    that and offers a "Kopier" button over the bottom of the screen for a few seconds,
    in every app, without a menu of each app's own. The shell's own conversation view
    selects the same way: hold a word, then drag the handles.
  - The history is kept in memory only. Copies a password manager marks as secret
    (x-kde-passwordManagerHint) aren't kept.
  - The agent reads and sets the clipboard through mobarchy-mcp (wl-clipboard). Reading it
    is an action, not a plain read, and isn't allowed while the phone is locked.
- squeekboard is removed from the image and the session.

## Consequences

- **Chromium** speaks text-input-v3 only with `--enable-wayland-ime
  --wayland-text-input-version=3`, so `/etc/chromium-flags.conf` sets both.
- **Room to grow:** the top strip is where word suggestions (a Norwegian dictionary, later
  the local model), dictation (the Whisper setup) and agent help will go.
- **Shell restarts:** the keyboard lives in the shell process, so a restart of the shell
  takes the keyboard with it for a second.
