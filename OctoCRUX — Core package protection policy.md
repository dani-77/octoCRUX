# OctoCRUX — Core package protection policy

Oct 19, 2026 · @Daniel Azevedo

OctoCRUX never removes a package that belongs to the CRUX core collection; users who really intend to remove one use `prt-get` or `pkgrm` from a terminal. This replaces the hardcoded essential-package list in `transaction_planner.cpp`.

## Policy

| Operation on a core package | OctoCRUX behaviour |
| --- | --- |
| Remove | Blocked. No override inside the GUI. |
| Update | Allowed (normal path). |
| Install (not installed) | Allowed; OctoCRUX never installs it on its own initiative. |

- **Escape hatch:** outside OctoCRUX only, via `prt-get remove` or `pkgrm` in a terminal. Whoever removes a core package must do it deliberately with the native tools.
- **Not installed is respected:** if a core package is absent, the user removed or never installed it for their own reasons; OctoCRUX does not reinstall it.
- **Owner decision (Daniel, 2026-10-19):** no typed-confirmation override in the GUI.

## Evidence

From the production fixture `printf-production-11token` (T480s, CRUX 3.8, 2026-10-19):

| Fact | Value | Consequence |
| --- | --- | --- |
| Ports in the core collection | 126 | The protected set is official, not invented |
| Core ports installed | 126 of 126 | Matches sysvd77's installer, which selects all core packages (`docs/INSTALLER-FLOW.md`, step 6) |
| Ports in the whole tree that declare `glibc` in `%e` | 1 (`libxcrypt`) | Declared dependencies cannot identify critical packages |
| Core ports with an empty `%e` | 68 of 126 | Core packages assume the core is present and do not declare it |

A "critical = many dependents" rule based on `prt-get dependent` would classify `glibc` as almost unused. Core membership is the reliable signal.

Caveat: on stock CRUX, `setup` lets the user deselect core packages, so "all core installed" holds on sysvd77 by design, not on every CRUX system. The policy does not depend on it.

## Implementation rules

1. **Check membership in the core collection, not the winning `%p`.** A core package overridden by an overlay would report the overlay's `%p` and silently lose protection. The rule is: a directory `<core collection>/<name>` exists. Today `prt-get dup` shows no core overrides (only `opt` and `contrib`), but the code must not rely on that.
2. **Discover the core collection from configuration.** Read the `prtdir` lines of `/etc/prt-get.conf` and select the collection named `core`. Do not hardcode `/usr/ports/core`. If no core collection is configured, fail closed: treat removal as blocked and report why.
3. **Scope: removals only.** Updates and installs of core packages follow the normal transaction path.
4. **One source of truth.** Delete the hardcoded essential-package list in `transaction_planner.cpp`; do not keep it alongside this rule.
5. **Error kind.** Return a dedicated `ProtectedPackage` outcome; the planner must never produce a `pkgrm` or `prt-get remove` argv for a protected package.

## User-facing message and tests

Message shown when a removal is blocked:

> *glibc belongs to the CRUX core collection. OctoCRUX does not remove core packages, because doing so can leave the system unable to run programs or boot. If you really intend to remove it, use `prt-get remove` or `pkgrm` from a terminal.*

Required tests (production fixture plus synthetic cases):

- [ ] Removing `glibc` returns `ProtectedPackage` and no removal argv is generated.
- [ ] Removing an installed non-core port (e.g. `tint2`) is planned normally.
- [ ] Updating an outdated core package is still planned (synthetic, since no core port is outdated in the fixture).
- [ ] A core package overridden by an overlay stays protected (synthetic).
- [ ] No `core` collection in `prtdir` blocks removal and reports the reason (synthetic).
- [ ] The hardcoded essential list is gone (grep check in review).
