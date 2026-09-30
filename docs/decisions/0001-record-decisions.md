# 0001: Record decisions as short ADRs

- Status: accepted
- Date: 2026-09-30

## Context

Several AI agents and humans work on Mobarchy in parallel and across sessions. Context that lives only in a chat is lost.

## Decision

Write every significant technical decision as a short numbered record in `docs/decisions/NNNN-title.md`, using the sections Status, Date, Context, Decision, and Consequences. If a decision changes, write a new record that supersedes the old one. Do not rewrite history.

## Consequences

Agents read `docs/decisions/` before making structural changes.
