# 0034: Calendar — the phone's own and the accounts' (CalDAV), as one

- Status: accepted
- Date: 2026-10-18

## Context

The owner wants a calendar on the phone, as a widget on the home board and an app (after
Notion Calendar: clean, the day as a timeline, events as blocks in their calendars' colours),
with the calendars of their accounts (one iCloud, three Google: ADR 0032), and Frigg able to
answer "what do I have tomorrow" and add an event.

## Decision

**iCalendar events, read and expanded in one place (the agent package's `calendars.py`),
from the phone's own calendar and the accounts' CalDAV calendars.**

- **The phone's calendar**: `~/.local/share/mobarchy/calendar/phone.ics`, written by
  `mobarchy-calendar` (add, update, delete), with times in the phone's named time zone
  (so a weekly event keeps its hour over summer time) and the zones' VTIMEZONEs.
- **The accounts' calendars**: `mobarchy-accounts` (ADR 0032) also finds each account's event
  calendars over CalDAV (RFC 4791: `calendar-home-set`, event calendars only, their colours
  from Apple's `calendar-color`) and syncs their events like the contacts: etags, then
  `calendar-multiget` for what changed, into `~/.cache/mobarchy/caldav/<id>.json`. iCloud at
  `caldav.icloud.com` with the same app-specific password, Google at
  `apidata.googleusercontent.com/caldav/v2/` (the OAuth sign-in asks for
  `calendar.readonly` too; the CalDAV API must be on in the Google Cloud project), other
  servers at their address. Read only for now: events are made in the phone's calendar.
  Each calendar can be turned off in the Accounts app.
- **Reading**: `mobarchy-calendar events --from <day> --days <n>` gives every occurrence in
  the range (repeating events expanded by `recurring-ical-events`, cancelled ones left out),
  in local time with the offset, each with its calendar's name and colour. One reader for the
  app, the widget and the agent; Python's iCalendar libraries rather than a C++ parser.
- **The shell**: `CalendarData` runs the command and follows the files (the phone's
  calendar, the synced events, `accounts.json`), so an event Frigg adds or a sync shows at
  once. The Calendar app (`mobarchy-shell --calendar`): a week strip with a dot per calendar,
  the day as a timeline (overlapping events side by side, all-day ones above, a line for
  now), the month as a grid, a sheet to add or change one of the phone's events (the day, all
  day or a time on wheels, how long, repeating, where, notes) and to see an account's. The
  home board's Calendar card: what is left of today and tomorrow; compact, the next one and
  when.
- **Frigg**: `calendar_events`, `calendar_add`, `calendar_update`, `calendar_delete` (the
  last one asks the owner).

## Consequences

- Writing to the accounts' calendars (CalDAV PUT) comes later; until then an event made on
  the phone stays on the phone.
- Reminders (alarms in events) are not rung yet.
