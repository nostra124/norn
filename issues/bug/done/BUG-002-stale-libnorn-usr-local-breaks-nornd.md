---
id: BUG-002
type: bug
severity: high
component: packaging
found_in: 0.12.1 (cff687a)
target_gate: v0.13.0
---

# BUG-002 — stale /usr/local/lib/libnorn.so.0 shadows installed lib, breaks nornd

- **Severity:** high
- **Component:** packaging
- **Found in:** 0.12.1 (cff687a)
- **Target gate:** v0.13.0

## Repro

```
# System previously had norn installed with --prefix=/usr/local
sudo make install          # now installs to /usr prefix (/usr/lib/x86_64-linux-gnu)
systemctl --user restart nornd
```

## Expected vs actual

**Expected:** `nornd.service` restarts cleanly after `make install`.

**Actual:** `nornd` fails to start with `exit-code` status 127:

```
/usr/bin/nornd: symbol lookup error: /usr/bin/nornd: undefined symbol: norn_routing_size
```

`ldd /usr/bin/nornd` shows the dynamic linker resolves `libnorn.so.0` to
`/usr/local/lib/libnorn.so.0` (a stale copy from a prior `--prefix=/usr/local`
install), **not** the freshly installed `/usr/lib/x86_64-linux-gnu/libnorn.so.0`:

```
libnorn.so.0 => /usr/local/lib/libnorn.so.0
```

`ldconfig -p` lists two providers; `/usr/local/lib` wins because it appears
earlier in the search path:

```
libnorn.so.0 (libc6,x86-64) => /usr/local/lib/libnorn.so.0
libnorn.so.0 (libc6,x86-64) => /lib/x86_64-linux-gnu/libnorn.so.0
```

The stale `/usr/local/lib/libnorn.so.0.0.0` predates the `norn_routing_size`
symbol introduced in the current build, so the new `nornd` binary fails to
locate it at runtime.

## Root cause

`make install` (and the distro packages) install `libnorn.so.0` into the
prefix libdir but do **not** remove a pre-existing `libnorn.so.0` left behind
in `/usr/local/lib` from a prior `--prefix=/usr/local` build. Because
`/usr/local/lib` precedes `/usr/lib/x86_64-linux-gnu` in the default linker
search path, the stale copy shadows the new one. The packaging has no
`preinst`/`dpkg`-cleanup or `make uninstall` step that would purge orphaned
shared libraries from alternate prefixes.

## Reproducibility

Only manifests when a prior install used `--prefix=/usr/local` (or the
universal install script, which defaults to `/usr/local`) and a subsequent
install targets `/usr`. The stale library survives `make install` into the
new prefix and silently takes precedence at load time.

## Suggested fix

Options (non-exclusive):

1. **`make install` cleanup** — have the install target detect and remove
   stray `libnorn.so*` in `/usr/local/lib` when installing under `/usr`
   (and vice-versa), or emit a warning instructing the user to remove them.
2. **Universal install script** — before installing, detect and purge prior
   copies in other prefixes (`rm -f /usr/local/lib/libnorn.so*`) so the new
   install is authoritative.
3. **Debian packaging** — `preinst` should remove any `/usr/local/lib/libnorn*`
   left by a source install before the new package lays down its files, or
   `Conflicts:` a hypothetical `norn-local` placeholder so the user is warned.
4. **Runtime guard** — `nornd` could verify at startup that the resolved
   `libnorn.so.0` provides the symbols it expects (e.g. via a versioned
   symbol check or `dladdr`) and print a clear diagnostic pointing at the
   stale library instead of a raw "undefined symbol" lookup error.

## Notes / fix

- Immediate workaround applied: `sudo rm -f /usr/local/lib/libnorn*` followed
  by `sudo ldconfig` clears the stale copy and `nornd` starts cleanly.
- `nm -D /usr/lib/x86_64-linux-gnu/libnorn.so.0.0.0` confirms the installed
  library exports `norn_routing_size`; the problem is purely a shadowing /
  stale-file issue, not a missing-symbol build defect.

## Fix (landed)

Two install paths now scrub prior copies in alternative prefixes before
laying down the new library, so the new install is authoritative:

1. **`scripts/install.sh`** — added `purge_stale_lib()`, invoked in `main()`
   between `run_tests` and `install`. It removes `libnorn.{so,so.0,so.0.0.0,a,
   la}` from `/usr/local/lib`, `/usr/lib`, and any `*-linux-gnu` multiarch
   libdir under them, skipping the active `$PREFIX`'s libdir, then refreshes
   `ldconfig`.
2. **`debian/norn.preinst`** — `install|upgrade` now also `rm -f`s any stray
   `/usr/local/lib/libnorn*` so the packaged `/usr/lib` copy is authoritative.

### Tests (TDD)

`tests/sit/build.bats` gains two static assertions (red-first, now green):

- "debian preinst purges stale /usr/local/lib/libnorn before install (BUG-002)"
- "install.sh purges stale libnorn from alternative prefixes before install (BUG-002)"

### Verification

- `bats tests/sit/build.bats` → 13/13 pass (incl. the two new tests).
- No regressions: the pre-existing failures in `nornd.bats`,
  `nornd_cluster.bats`, and `coverage.bats` reproduce on unmodified `master`.