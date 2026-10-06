# 0023: Mobarchy's own browser, on QtWebEngine

- Status: accepted
- Date: 2026-10-06

## Context

On the phone, the desktop browsers are hard to use (the owner's experience on comet):
- Text is small, and sites get their desktop versions.
- Tabs are hard to hit and to move between.
- The address bar is small and hard to read, and the menus are small.

Firefox with postmarketOS's mobile configuration and GNOME Web make that better but not
good: their chrome is still a desktop browser's.

Android browsers would need a whole Android userspace for apps (Waydroid), apart from the
rest of the phone: its keyboard, the agent, sharing, and the owner's 1Password.

## Decision

Mobarchy gets its own browser, `mobarchy-browser` (`os/browser`):
- **Engine: QtWebEngine** (Chromium), from the distribution (`qt6-webengine`, ARM64 too).
  It is the same toolkit as the shell (ADR 0009), with GPU rendering through Mesa
  (Panthor on comet).
- **A phone's web:** a mobile user agent and a phone-sized viewport. The page's zoom makes
  the CSS width a phone's (about 400 px wide on the outer screen, more on the inner one),
  so sites lay themselves out for a phone and text is readable. Touch scrolls and pinches.
- **Chrome for thumbs:** everything is at the bottom.
  - One big bar: back, the address or title (a tap edits it full width, with suggestions
    from history), tabs, and a menu.
  - Tabs are a grid of cards.
  - Forward and back are a swipe along the bar.
  - The bar hides while scrolling down and returns while scrolling up. It floats over the
    page: the page keeps its size, so showing or hiding the bar never lays it out again.
  - Sizes follow the shell's (Theme).
- **Phone integration:**
  - Mobarchy's keyboard, through text input, as everywhere.
  - Links from other apps open in it (the default `x-scheme-handler/http(s)`).
  - Downloads go to ~/Downloads.
  - The camera and microphone go through the browser's own permission prompt.
- **For the agent:** a D-Bus interface (`org.mobarchy.Browser`): open a URL, list tabs,
  read the current page's title, URL and text. The agent can then use the web the
  owner sees, with the owner's sessions, instead of a hidden browser.
- **Passwords: 1Password through its CLI.** QtWebEngine 6.11 loads Chrome extensions but
  gives them no Chrome APIs (`chrome.runtime` is empty), so 1Password's extension stays blank.
  - The browser asks `op` instead (os/packages/1password-cli), which the 1Password app
    unlocks ("Integrate with 1Password CLI").
  - A login page gets a key in the bar, which lists that site's logins. A tap fills in the
    username and password. A login with a one-time code offers the code on the page that
    follows.
  - Nothing is stored by the browser.

## Consequences

- QtWebEngine is about 270 MB installed.
- Mobarchy maintains a browser's chrome, not its engine: security updates come with Qt.
- Sites that check for Chrome get a Chrome-on-Android user agent; QtWebEngine is Chromium,
  so that is true of the engine.
- Firefox stays in the image for what needs it (DRM, extensions).
