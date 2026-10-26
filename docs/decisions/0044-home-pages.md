# 0044: Home as pages: notifications, widgets, apps

- Status: accepted (trial; the owner's choice, 2026-10-26)
- Date: 2026-10-26

## Context

Two things lived in sheets over the screen:
- **The notifications** were a page inside quick settings: pull down the status bar, then
  tap "N notifications".
- **The installed apps** were a drawer. It followed the finger up from the bottom edge,
  over whatever was on screen. The open apps (the switcher) took the same swipe, with a
  hold before letting go.

The owner wanted both as pages of home, beside the widgets.

## Decision

- **Home is three pages side by side:** the notifications, the widgets and the apps
  (`Home.qml`: `NotificationsPage`, `HomeBoard`, `AppsPage`).
  - Home opens on the widgets. The notifications are a swipe right from them, the apps a
    swipe left.
  - Going home (a tap on the bottom edge) always comes back to the widgets.
  - Dots at the bottom show the page. The notifications' dot is lit while there are any.
- **The apps page** is the drawer's content: the search the shell's keyboard types into,
  Mobarchy's own apps first, and "New window" when an icon is held. Leaving the page ends
  the search.
- **The bottom edge's swipe up is the switcher alone.** It follows the finger from the
  first few pixels; there is no hold to tell it from the drawer any more. A tap still goes
  home.
- **Laptop mode's Super key** goes home to the apps page, searching. Pressed again, it goes
  back to the widgets.
- The drawer (`Drawer.qml`) is gone, and so are the notifications in quick settings. The
  status bar still shows the bell and the count.

## Consequences

- **Fewer gestures to the open apps:** they are one plain swipe up from anywhere.
- **More gestures to start an app from inside another:** a tap home, then a swipe left
  (the switcher or the recent apps card for the ones used lately).
- **Nothing slides up over an app any more** but the switcher. Quick settings is the only
  sheet left (`Sheet.qml`).
- **Paging is off while the widgets are being edited** (their cards are dragged).
