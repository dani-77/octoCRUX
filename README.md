# OctoCRUX

Native Qt 6 graphical package manager for CRUX Linux 3.8 (`prt-get`, `pkgutils`, `ports`), replicating the Octopi/OctoXBPS desktop workflow while strictly respecting CRUX's source-based philosophy, minimalist design, and multi-collection overlay semantics.

---

## Overview & Design Philosophy

OctoCRUX provides a clean, responsive desktop interface for CRUX package management without compromising the system's core principles:

1. **Native Tooling Compliance:** Does not replace or re-implement CRUX tools (`prt-get`, `pkgmk`, `pkgutils`, `ports`). Operations are executed via the native toolchain.
2. **Decoupled Architecture:** Strict separation between pure data parsers, operations planning, process execution, and the Qt 6 user interface.
3. **Three-Axis Domain Model:** Captures CRUX state orthogonally:
   - **Axis 1 (Install Status):** `Installed`, `NotInstalled`, `Outdated` (derived from `prt-get %i`).
   - **Axis 2 (Lock Status):** `Locked`, `Unlocked` (derived from `prt-get %l` and `/var/lib/pkg/prt-get.locker`).
   - **Axis 3 (Virtual Alias):** Virtual ports satisfied by concrete installed providers (e.g. `rust` satisfied by `rust-bin` from `/etc/prt-get.aliases`).
4. **Single-Elevation Transaction Batching:** Multi-package installations and updates are batched into native commands (`prt-get depinst target1 target2 ...` or `prt-get update target1 target2 ...`). Dependency resolution is handled natively by `prt-get`, prompting for credentials once per operation type. Composite transactions containing both updates and installs execute updates first, then installs (two sequential elevations).
5. **Asynchronous Non-Blocking Execution & Live Streaming:** Mutations run in a dedicated asynchronous process runner (`CruxProcessRunner`). stdout and stderr stream live line-by-line into the GUI Output console with zero UI freezing, tracking compilation phases (`=======> ...`) and with no arbitrary timeouts for long compilations.
6. **Graceful Process Lifecycle:** Cancellation requests a clean shutdown via SIGTERM (`QProcess::terminate()`), avoiding blunt SIGKILL. Cross-UID process tree signal propagation through elevation wrappers (`qt-sudo` -> `sudo` -> `pkgmk`) remains an open investigation `[open]` pending bare-metal container verification.
7. **Authoritative Core Package Protection:** Fail-closed policy dynamically discovering the `core` collection directory from `/etc/prt-get.conf`. Any package residing in `core` (even if overridden by higher-priority overlays) is strictly blocked from GUI removal.

---

## Features

- **Clean-Room Modern Qt 6 UI (Option C):** Hand-crafted, modular C++ widget architecture without brittle `.ui` XML files, designed for maintainability and testability.
- **Multi-Axis Status Filtering:** Instant toolbar switching across *All*, *Installed*, *Outdated*, *Locked*, and *Available* ports.
- **Multi-Collection Overlay Support:** Dynamic discovery and filtering across CRUX repository overlays (`core`, `opt`, `xorg`, and custom collections like `d77crux`).
- **Real-Time Substring Search:** Fast case-insensitive search across port names and descriptions.
- **Detailed Inspection Tabs:**
  - **Information:** Formatted overview of package metadata, versions, release, collection, dependencies, virtual aliases, and pre/post-install script flags (`%E`, `%O`).
  - **Files:** Complete file inventory for installed packages via `pkginfo -l`.
  - **Pkgfile:** Read-only view of the port build recipe.
  - **Footprint:** Expected footprint inspection (`.footprint`).
  - **Readme:** Port documentation (`README`) when declared.
  - **Output:** Live monospaced streaming terminal showing real-time transaction output and compilation logs.
- **Dynamic Privilege Escalation:** Auto-discovery of desktop elevation tools (`qt-sudo` -> `sudo` -> `doas` -> `su`), with POSIX single-quote shell escaping to prevent injection vulnerabilities. *Settings...* in the toolbar can pin one helper instead (see "Settings").
- **Post-Transaction Audit:** Automatic inspection of `/var/lib/pkg/rejected` for non-overwritten configuration files after updates.

---

## Building & Installation

### Requirements

- Linux operating system (CRUX 3.8 target, Void Linux development host)
- C++17 compliant compiler (GCC 14+ or Clang 18+)
- CMake 3.22+
- Qt 6 (Core, Widgets, Network; Test for the test suite)
- CRUX userland tools (`prt-get`, `pkgutils`, `ports`) for production execution

### Compiling

```bash
# Clone the repository
git clone https://github.com/dani-77/octoCRUX.git
cd octoCRUX

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The resulting binaries will be:
- `build/octocrux`: Main graphical application (`--upgrade-all` opens the Upgrade All preview).
- `build/octocrux-notifier`: System tray update notifier.
- `build/octocrux_live_check`: Standalone diagnostic tool for bare-metal verification (not installed).

### Installing

On CRUX, use the port in [`packaging/crux/`](packaging/crux/README.md), which builds from a release tag. To install from a checkout instead:

```bash
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build -j$(nproc)
sudo cmake --install build
```

This installs both binaries, their desktop entries, the icon and the polkit action `org.octocrux.ports-sync`. With `-DOCTOCRUX_INSTALL_DOCS=ON` (the default) it also installs the README and the sudoers example; the CRUX port turns that off.

---

## Settings

*Settings...*, at the end of the toolbar, holds the general options. They are stored in `~/.config/OctoCRUX/octoCRUX.conf`, which the notifier shares, and apply at once. The button is disabled while an operation runs.

- **Privilege helper:** *Automatic* (the default) uses the first of `qt-sudo`, `sudo`, `doas`, `su` found in `PATH`. Picking one makes OctoCRUX use it for every privileged operation, and the notifier's "Sync ports tree now" and *Options…* use it too. If the chosen helper is no longer in `PATH`, OctoCRUX uses automatic detection and says so in the Output tab. `sudo`, `doas` and `su` need a terminal to ask for a password, so choosing one shows the same warning as at start. Scheduled syncs ignore this setting: they only use pkexec or `sudo -n`, see below.
- **Cancel grace period** (5–120 s, default 10): after Cancel, how long OctoCRUX waits before warning that the operation is still running. It never kills the privileged process; pressing Cancel again asks it to stop again.

## Notifier and automatic sync

`octocrux-notifier` sits in the system tray. It shows how many packages Upgrade All would update (locked ones are counted apart) and how long ago the ports tree was synchronized. Reading needs no privileges: it uses `prt-get printf`. It never installs, upgrades or removes packages. Its "Upgrade All..." entry opens OctoCRUX on the Upgrade All preview (`octocrux --upgrade-all`), and nothing runs until you press Apply there.

Start it with `octocrux-notifier &`. Starting it with the session is up to each user and is not enabled by default. To enable it, copy the desktop file into your autostart directory:
```sh
mkdir -p ~/.config/autostart
cp data/octocrux-notifier.desktop ~/.config/autostart/
# until OctoCRUX is installed system-wide, point Exec= at the build:
sed -i "s|^Exec=.*|Exec=$HOME/octoCRUX/build/octocrux-notifier|" ~/.config/autostart/octocrux-notifier.desktop
```
To disable it, remove that file.

**Syncing** (`ports -u`) needs root, because it rewrites `/usr/ports`. "Sync ports tree now" in the tray menu runs it through the privilege helper (qt-sudo by default, or the one chosen in OctoCRUX *Settings...*), which may ask for a password. Automatic sync is **off** by default; turn it on in *Options…* (once a day is suggested). A scheduled sync never shows a password dialog. It runs only if one of these allows `/usr/bin/ports -u` without a password, and otherwise only reminds you:

1. **polkit (recommended).** *Options…* offers to install a rule that lets **your user only**, in a **local, active session**, run exactly `/usr/bin/ports -u` (action `org.octocrux.ports-sync`).
   polkit only knows a session is local and active if a session tracker registered it. On CRUX with sysvinit that is ConsoleKit2, or elogind. If *Options…* says that polkit "does not see this desktop session as local and active", start your X session through ConsoleKit, for example in `~/.xinitrc`:
   ```sh
   exec ck-launch-session dbus-launch --exit-with-session <your window manager>
   ```
   After logging in again, `ck-list-sessions` should list your session with `active = TRUE`, and `pkcheck -a org.octocrux.ports-sync -p $$` should exit 0 once the rule is installed.
2. **sudo.** If sudoers lets your user run `/usr/bin/ports -u` without a password, the notifier uses `sudo -n /usr/bin/ports -u`. A minimal entry is in `contrib/sudoers.d/octocrux-ports-sync.example`. A broad `NOPASSWD: ALL` rule also works, but grants far more than this.

## Running the Automated Test Suite

OctoCRUX enforces strict empirical verification and the **SCOFS Protocol** (*Special Case Oh Foda-Se*). All parsers, operations planners, process runners, and GUI components are covered by 11 automated test suites:

```bash
ctest --test-dir build --output-on-failure            # all suites (~40 s)
ctest --test-dir build --output-on-failure -LE slow   # routine run: skips suites labelled 'slow'
```

`test_live_transaction_execution` carries the CTest label `slow` (it runs a simulated build for about 36 s). Run it before committing changes to the execution engine.

### Test Suites Included:

| Test Suite | Purpose |
| :--- | :--- |
| `test_printf_parser` | Canonical 11-token `prt-get printf` parsing, strict schema enforcement, and embedded tab protection. |
| `test_aliases_parser` | Virtual alias resolution (`/etc/prt-get.aliases`) and bidirectional provider mapping. |
| `test_dependency_parser` | Direct and declared dependents parsing, recursive install order, and alias substitution. |
| `test_crux_adapter` | Abstract backend queries, POSIX ERE pattern escaping (`escapeErePattern`), async mutations, and the transaction preview commands (order, grouping, privilege wrapping, invocations split by build check exceptions). |
| `test_package_model` | `QAbstractTableModel` and `QSortFilterProxyModel` multi-axis filtering and sorting. |
| `test_mainwindow` | Offscreen UI verification of catalog population, dock tabs, search, core protection tooltips, close refusal during operations, the transaction preview gate (Install/Upgrade/Upgrade All/Sync start no process until confirmed; fake `prt-get`/`ports` in `PATH`), Upgrade vs Upgrade All enablement, no stale buttons after a reload, status bar layout, in-place progress lines, per-package build check exceptions (Information tab, flags in the preview and the command, reset after the run), and the "Retry with -in" offer after NEW-only footprint failures. |
| `test_transaction_planner` | Dependency partitioning, locked outdated package skipping, mount inspection, and fail-closed core protection. |
| `test_build_log_analyzer` | Authentication failures from real qt-sudo/sudo output (`tests/fixtures/crux3.8/qtsudo-auth/`); footprint failure extraction (NEW-only vs MISSING); log-text classification of real `pkgmk` 5.40.12 failure output (`tests/fixtures/crux3.8/pkgmk-failures/`, generated by `tools/gen_pkgmk_failure_logs.sh`): build, unpack, download, md5sum, signature, footprint, Pkgfile, directory, `ENOSPC`, interruption. Exit code values are never mapped. |
| `test_privilege_helper` | Discovery priority (`qt-sudo -d`, `sudo`, `doas`, `su`), POSIX single-quote shell escaping safety, and resolving the helper chosen in Settings (with fallback to detection when it is not in `PATH`). |
| `test_crux_process_runner` | Asynchronous line-by-line stdout/stderr streaming, phase parsing, graceful termination, stdin EOF for interactive prompts, and `\r` progress meters (curl) reduced to their visible state. |
| `test_sync_schedule` | Notifier sync scheduling (Never default, once a day, once a day at H with catch-up, every N minutes with a 60-minute floor, clock moved back) and shared settings round trip. |
| `test_notifier` | Notifier logic on the real catalog fixture: read-only update summary (unlocked vs locked), notification only when the count grows, manual `ports -u` records the sync time. Scheduled sync with fake `pkcheck`/`pkexec`/`sudo`/`ports`: never by default; pkexec when polkit authorizes; `sudo -n` fallback; reminder only (no retry storm) when neither works; skipped without network or while a package tool runs. Polkit rule text and shipped action definition. |
| `test_live_transaction_execution` | Integration test with a simulated slow `prt-get`: verifies execution > 30s without timeouts, live line streaming, that cancellation delivers `SIGTERM` to the directly spawned process (a fake script, unprivileged, no helper), and single-command batching. It does **not** show that a real `qt-sudo`/`sudo` → `prt-get` → `pkgmk` tree stops without orphans; that is open test 2.10.3. |

---

## Governance & Collaboration

Development is governed by the working agreement in [`AGENTS.md`](AGENTS.md). All architectural decisions, empirical findings, and session logs are recorded chronologically in [`NOTES.md`](NOTES.md).
