---
id: FEAT-032
type: feature
priority: medium
complexity: M
estimate_tokens: 40k-90k
estimate_time: 60-150min
phase: planned
status: done
depends_on: [FEAT-029]
milestone: MILESTONE-0.12.0
spawned_from: ~
---
# Packaging — nornd as a user **and** system daemon (systemd + launchd)

## Description

**As an** operator (or a laptop user)
**I want** `nornd` to install and run as both a per-user daemon and a system
daemon, socket-activated
**So that** `norn` "just works" against a running node on first use, on Linux
and macOS.

## Implementation

- **systemd (Linux)** — socket-activated units shipped under `contrib/systemd/`:
  - System: `nornd.socket` (`/run/nornd/nornd.sock`) + `nornd.service`
    (`Type=notify`, runs as the dedicated `norn` user, sandboxed,
    `StateDirectory=nornd`). Identity defaults to the **host SSH key**
    (`/etc/ssh/ssh_host_ed25519_key`).
  - User: `contrib/systemd/user/{nornd.socket,nornd.service}` (socket at
    `%t/nornd.sock`); identity = ssh-agent or `~/.ssh/id_ed25519`.
- **launchd (macOS)** — `contrib/launchd/io.norn.nornd.plist` (system daemon,
  `_norn` user, host key) and `io.norn.nornd.user.plist` (user agent), both with
  `Sockets` socket activation.
- **nornd socket activation** — accept a passed-in listening fd
  (`sd_listen_fds` / launchd `launch_activate_socket`) instead of binding its
  own, and `sd_notify(READY=1)` for `Type=notify`. (Daemon-side hook in
  FEAT-029.)
- **install plumbing** — `make install` installs the units/plists and creates
  the `norn`/`_norn` service user; package metadata (Homebrew formula, Debian/
  RPM) enables the user agent / system service appropriately. Document
  `systemctl [--user] enable --now nornd.socket` and `brew services start norn`.

## Acceptance Criteria

1. `systemctl --user enable --now nornd.socket` starts nornd on the first
   `norn cluster …` call (socket activation), as the user, with the user's SSH
   identity.
2. `sudo systemctl enable --now nornd.socket` runs the system daemon as `norn`
   with the host SSH key identity, socket at `/run/nornd/nornd.sock`.
3. macOS: the user agent and system daemon load via launchd and are
   socket-activated.
4. Units pass `systemd-analyze verify`; clean stop removes the socket.

## Cross-repo

How the fleet actually ships nornd — servers as a system service keyed on the
host SSH key, workstations/laptops as a user service keyed on the user's SSH key.
