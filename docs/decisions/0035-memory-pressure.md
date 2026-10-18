# 0035: Compressed swap, and systemd-oomd for the apps

- Status: accepted
- Date: 2026-10-18

## Context

On 2026-10-17 the phone froze again: the front screen black, the inner one stuck, the phone
hot. Memory was full. Most of it (7.7 GB) was GPU memory that apps held for shells that
had died: an app the shell started inherited the shell's open files, its GPU render node
among them, so a dead shell's buffers stayed pinned. That leak is fixed in the shell
(apps start without its open files, `launch.h`). The phone still had nothing to stop
the next thing that fills it.

- Comet has 16 GB and no swap. With nothing to page out but code, a phone short of memory
  thrashes for minutes before the kernel's OOM killer picks something.
- The per-app cap (ADR 0027) stops one app, not several together.
- systemd-oomd was installed but not enabled. ADR 0027 left it out because the shell ran in
  `app.slice`, beside the apps, where oomd would pick from.

## Decision

**Compressed swap in memory (zram), and systemd-oomd kills an app when the phone has been
short of memory for a while.** The shell and the agent are moved out of the apps' slice.

- **zram** through `zram-generator`: `/usr/lib/systemd/zram-generator.conf`, a `zram0` of
  half the memory, zstd. Pages the apps have not touched for a while are compressed
  (about a third of their size) instead of code being thrown out and read back.
  `vm.swappiness = 180` (swapping to memory costs some CPU, no disk) and
  `vm.page-cluster = 0` (no read-ahead), in `/usr/lib/sysctl.d/50-mobarchy-zram.conf`.
  Android, which this hardware was made for, does the same.
- **systemd-oomd** is enabled. The user manager's `app.slice` (every app, ADR 0027) has
  `ManagedOOMMemoryPressure=kill` at 50 %, and oomd acts after 10 s, not 30 (a phone
  stuck for 30 s is a frozen phone). oomd then kills the app in `app.slice` that is causing
  the pressure.
- **The shell, the MCP server and the keyring setup run in `session.slice`** (`Slice=` in
  their units), the user manager's slice for what the session cannot do without. oomd does
  not pick from it. Hyprland is the system's `mobarchy-session.service`, outside the user
  manager altogether.

The local model (`mobarchy-llm`) stays in `app.slice`. It is large and can be started
again (`Restart=on-failure`).

## Tested

On the simulator (4 GB, zram 1.9 GB): oomd lists `…/user@1000.service/app.slice` as
monitored, and the shell's cgroup is `…/session.slice/mobarchy-shell.service`. Three
processes each allocating incompressible memory as fast as they could were killed in
turn; the shell kept running (not restarted). That allocation was fast enough that the
kernel's OOM killer acted before oomd's 10 s; oomd is for the slow case, the phone that
thrashes without running out.

## Consequences

- The phone has more memory in practice (what is idle is compressed), and when it still
  runs short, an app is closed rather than the phone freezing.
- An app that oomd kills is gone without a word. A notice from the shell would be good
  (oomd logs the kill; the unit fails with `oom-kill`).
- ADR 0027's "Not done" on systemd-oomd is done here.
