# 0024: The owner approves the agent's risky steps, and root, on the screen

- Status: accepted
- Date: 2026-10-06

## Context

The agent's tools have risk levels (ADR 0004). `confirm` (irreversible or outward-facing:
`run_shell`, and later calls, messages, payments) needs the owner's consent. There was no
way to ask, so the policy denied `confirm` outright. The agent could not run a command, let
alone install anything.

Root takes more. sudo asks for a finger or the PIN (ADR 0022), but a command the agent runs
has no terminal to ask on. Nothing on the phone answered polkit either, so apps that ask
polkit (1Password's system unlock, pkexec, run0) failed.

## Decision

**The shell asks, the tool layer enforces.**
- Policy decision `ask` (the new default for `confirm`): mobarchy-mcp asks the shell over
  D-Bus, `org.mobarchy.Approvals.Ask(tool, title, detail)` on the session bus. It waits
  for the answer (two minutes, then no).
- The shell shows a sheet over everything: what the agent wants to do, with the command
  in full for `run_shell`. The answers:
  - **Allow**: this call.
  - **Allow for this task**: this tool, for the rest of the agent's run (its MCP session).
  - **Deny**.
- A locked phone answers no: the agent's request ends with "the phone is locked" (as the
  screen tools do, ADR 0013).
- No shell running, or no answer, is a no. `deny` in the policy still means never asking;
  `allow` means not asking.

**Root through polkit, answered by the shell.**
- The shell is the session's polkit authentication agent (polkit-qt6). An action that
  needs the owner (`auth_admin`, and pkexec's `org.freedesktop.policykit.exec`) gets a
  sheet: polkit's message (with pkexec, the command) and the PIN pad. A finger on the sensor
  works too.
- `/etc/pam.d/polkit-1` is sudo's stack without the ssh exception: pam_fprintd, then the
  PIN (system-auth).
- Finger or PIN, whichever comes first. PAM asks one module at a time, and pam_fprintd
  waits for the finger. A PIN typed first ends that wait:
  - the shell calls `org.mobarchy.Fingerprint.SkipVerify`;
  - the verify ends as "disconnected", and pam_fprintd steps aside;
  - pam_unix then asks for the password, and the shell gives it the PIN.
- On a locked phone the request waits for the unlock: polkit's own timeout ends it if the
  owner doesn't come.
- **The agent uses pkexec for root**, not sudo (the tool's instructions say so). An agent
  command that needs root gets two sheets: the agent's step (Allow), then root (finger or
  PIN). The first is consent to the agent's action. The second is the owner's credential,
  which the agent never sees.

## Consequences

- The agent can install packages, change system settings and run what the owner allows,
  step by step on the screen.
- Apps that use polkit work: 1Password's system unlock (finger), pkexec, run0, and
  systemd and NetworkManager actions that ask the admin.
- An approval gives the agent's command the owner's rights (and root with the second
  sheet). The sheet shows the full command; reading it is the owner's part.
- Asking needs the shell. Over ssh, nothing changes: sudo still lets the developer's key
  through (ADR 0022), and pkexec there has its own terminal prompt.

## Since (2026-10-06)

- **The shipped policy allows the agent's steps** (`confirm = "allow"`): asking at every
  command was too much in use. The sheet comes back with finer rules (kinds of commands).
  Root is unchanged: pkexec asks for a finger or the PIN.
- **comet: polkit's agent helper is setuid.** polkit 127 starts its helper through a socket
  that needs socket pidfds (Linux 6.5), and comet's kernel is 6.1, so the helper refused.
  On comet the socket is masked and the helper is setuid again, as before polkit 126
  (`devices/comet`: rootfs-hook, a pacman hook for polkit upgrades).
