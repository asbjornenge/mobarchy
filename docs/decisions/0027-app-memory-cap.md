# 0027: Each app has a memory cap

- Status: accepted
- Date: 2026-10-10

## Context

On 2026-10-10 the phone (comet: 16 GB, no swap) froze for about three minutes after an
unfold. A web page's renderer had grown to 4.3 GB (chattr in the browser; the same page as
a web app was at 1.4 GB after 13 minutes), and the new screen size made the pages draw
again. Memory ran out. With no swap the kernel can page out only code, so everything
slowed to a crawl until its OOM killer chose the renderer. The shell died in the crush
too (SIGBUS on a shared-memory page it could not get) and restarted.

Nothing stopped one app from taking the whole phone. systemd-oomd is installed but not
enabled. The shell itself runs in `app.slice`, beside the apps, so a cap on the slice
would cap the shell too.

## Decision

**Every app the phone starts runs in a unit named `app-mobarchy-<app>-<n>`, and each such
unit may use at most 30 % of the memory** (`MemoryMax=30%`: about 4.7 GB on comet). A
drop-in for the name prefix does it for all of them, the transient units included:
`/usr/lib/systemd/user/app-mobarchy-.scope.d/50-memory.conf` and the same for `.service`.

- The shell already starts apps as `app-mobarchy-<desktop id>-<n>.scope`.
- `mobarchy-open` (`$BROWSER`, xdg-open) and the agent's browser tool now name their units
  the same way. Before, they were `run-*`, without a cap.

An app that reaches its cap is killed by the kernel within its own cgroup at once (a test
allocating without limit under a 300 MB cap was killed in 6 s). The rest of the phone goes
on. A browser's renderer that is killed is a crashed tab, not a crashed browser.

A percentage, not a size, so it fits any device without device code.

## Not done (yet)

- **systemd-oomd** (kill on memory pressure, before the cap). It picks the unit to kill
  among a slice's, and the shell is in `app.slice` with the apps. For a user unit,
  `ManagedOOMPreference=avoid` is honoured only on cgroups owned by root, so the shell
  could not be kept out. Several apps together can still fill the phone; if that happens
  in use, move the shell to a slice of its own and enable oomd for `app.slice`.
- **The Android container** (telephony, camera) is a system service, outside this.

## Consequences

- One app can no longer freeze the phone. It can still use a lot: 30 % is generous for a
  browser with many tabs.
- An app that needs more (a local model in an app, say) has to be started outside the
  prefix, or given its own drop-in.
