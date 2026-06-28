---
id: BUG-001
type: bug
severity: high
component: packaging
found_in: 0.12.0 (cd4a234)
target_gate: v0.13.0
---

# BUG-001 — nornd system service not started after deb package install

- **Severity:** high
- **Component:** packaging
- **Found in:** 0.12.0 (cd4a234)
- **Target gate:** v0.13.0

## Repro

```
sudo dpkg -i norn_0.12.0_amd64.deb
sudo systemctl status nornd
```

## Expected vs actual

**Expected:** `nornd.service` is active (running) after installation.

**Actual:** `nornd.service` is inactive (dead). The postinst enables the unit
(symlinks created) but fails to start it. `deb-systemd-invoke` reports:

```
Could not execute systemctl:  at /usr/bin/deb-systemd-invoke line 148.
```

Attempting a manual `sudo systemctl start nornd.service` fails:

```
A dependency job for nornd.service failed.
```

The socket unit `nornd.socket` fails because the `norn` system user does not
exist:

```
× nornd.socket - norn node daemon IPC socket (system)
     Active: failed (Result: exit-code)
```

## Root cause

The systemd unit files (`nornd.service` and `nornd.socket`) declare:

```ini
User=norn
Group=norn
SocketUser=norn
SocketGroup=norn
```

but the Debian packaging does **not** create the `norn` system user or group.
No `preinst` maintainer script exists in `debian/` to run `adduser --system
--group norn`, so systemd cannot resolve the user/group and the service
fails to start.

## Fix

1. `src/nornd/main.c` — added `auto_generate_identity()` which calls
   `ssh-keygen -t ed25519` to create an identity key when no `--identity`
   is given and the default path doesn't exist. `default_identity()` falls
   back to `/var/lib/nornd/identity` for system users (HOME is /nonexistent).
2. `debian/norn.preinst` — creates the `norn` system user and group before
   the package is configured.
3. `contrib/systemd/nornd.service` — removed `--identity` flag so the
   daemon auto-generates; removed `SupplementaryGroups=ssh_keys` (no longer
   needed); `ProtectSystem=strict` is compatible because the identity lives
   under the `StateDirectory` (writable).
4. `tests/sit/build.bats` — added static assertions that the preinst script
   exists, creates the norn user, and that the systemd units reference the
   `norn` user and group.
