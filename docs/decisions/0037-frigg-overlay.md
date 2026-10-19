# 0037: Frigg is one layer over everything, live or in text

- Status: accepted
- Date: 2026-10-19

## Context

Home had the conversation with the agent in a pill at the bottom that grew into a sheet
(text), and Frigg's live conversation (ADR 0026) was a separate overlay. The owner wanted
one place: tap Frigg, talk; switch to text when wanted; switch or start conversations in
both. A first try kept the text at home (a normal window, for text input, ADR 0006) and
borrowed the overlay for live: it showed as two apps, with the orb popping between them.

## Decision

**Frigg is a single layer surface over everything (`TalkOverlay` hosting `FriggView`).**

- Opened by the home board's Frigg card (live) or by "Hei Frigg" (live). Home is the
  widgets only; the pill is gone.
- Live: her orb large (`FriggStage`: what she hears and says, "Hei Frigg" and voice
  settings). "Aa" goes to text: the live conversation ends and the orb glides into the top
  right corner; the feed and the field appear. The small orb goes back to live at once.
- At the top in both: the agent chip, earlier conversations, a new conversation. On the
  lock screen only live, nothing of the conversations.
- ✕ or a pull down closes it all (and ends a live conversation); so does going home.
- The field is typed into with the shell's own keyboard capturing the keys (as the
  drawer's search): Hyprland gives no text input to layer surfaces (ADR 0006). Typing,
  deleting and pasting work; moving the cursor inside the text does not (yet).
- The layer keeps clear of the status bar and the keyboard (exclusion zone 0).
