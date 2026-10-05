# 0022: sudo asks the owner, except over ssh

- Status: accepted
- Date: 2026-10-05

## Context

Images gave group wheel sudo without a password (`NOPASSWD`), for development over ssh.
Then anything running as the owner, any app, has root without asking. Now the phone has a
PIN, which is the owner's password (ADR 0013), and a fingerprint sensor (ADR 0021).

Development needs sudo over ssh without a prompt: tools/repo-build's device builder,
tools/device-drift, and the developer's work on the phone. ssh logs in with a key only
(sshd: no passwords, no root login).

## Decision

- **sudo asks**: `%wheel ALL=(ALL:ALL) ALL`. On the phone it takes a finger on the sensor
  (pam_fprintd, against fprintd's interface, ADR 0021) or the PIN.
- **Except in an ssh session.** `/etc/pam.d/sudo` first runs `pam_exec`
  `/usr/lib/mobarchy/sudo-remote`, which lets sudo through when the process calling it is
  in a remote session of sshd's. It asks logind for that session (`sd_pid_get_session`, from
  the process's cgroup), so an app on the phone can't claim one, as it could with
  environment variables. The ssh key already is the developer's root access; that
  doesn't change.
- Tools call `sudo`, not `sudo -n`: `-n` refuses before PAM runs, whenever the sudoers rule
  wants authentication.

## Consequences

- An app running as the owner no longer has root for the asking.
- Losing the ssh key is as bad as before (it is root on the phone), and no worse.
- The simulator keeps its own passwordless sudo (cloud-init, sim/mobsim): it has no owner
  to ask.
