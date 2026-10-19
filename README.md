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
- **Dynamic Privilege Escalation:** Auto-discovery of desktop elevation tools (`qt-sudo` -> `sudo` -> `doas` -> `su`), with POSIX single-quote shell escaping to prevent injection vulnerabilities.
- **Post-Transaction Audit:** Automatic inspection of `/var/lib/pkg/rejected` for non-overwritten configuration files after updates.

---

## Building & Installation

### Requirements

- Linux operating system (CRUX 3.8 target, Void Linux development host)
- C++17 compliant compiler (GCC 14+ or Clang 18+)
- CMake 3.22+
- Qt 6 (Core, Widgets, Test)
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
- `build/octocrux`: Main graphical application.
- `build/octocrux_live_check`: Standalone diagnostic tool for bare-metal verification.

---

## Running the Automated Test Suite

OctoCRUX enforces strict empirical verification and the **SCOFS Protocol** (*Special Case Oh Foda-Se*). All parsers, operations planners, process runners, and GUI components are covered by 11 automated test suites:

```bash
ctest --test-dir build --output-on-failure
```

### Test Suites Included:

| Test Suite | Purpose |
| :--- | :--- |
| `test_printf_parser` | Canonical 11-token `prt-get printf` parsing, strict schema enforcement, and embedded tab protection. |
| `test_aliases_parser` | Virtual alias resolution (`/etc/prt-get.aliases`) and bidirectional provider mapping. |
| `test_dependency_parser` | Direct and declared dependents parsing, recursive install order, and alias substitution. |
| `test_crux_adapter` | Abstract backend queries, POSIX ERE pattern escaping (`escapeErePattern`), and async mutations. |
| `test_package_model` | `QAbstractTableModel` and `QSortFilterProxyModel` multi-axis filtering and sorting. |
| `test_mainwindow` | Offscreen UI verification of catalog population, dock tabs, search, and core protection tooltips. |
| `test_transaction_planner` | Dependency partitioning, locked outdated package skipping, mount inspection, and fail-closed core protection. |
| `test_build_log_analyzer` | Classification of `pkgmk(8)` exit codes 2–9, `ENOSPC`, footprint mismatches, and signify errors. |
| `test_privilege_helper` | Discovery priority (`qt-sudo -d`, `sudo`, `doas`, `su`) and POSIX single-quote shell escaping safety. |
| `test_crux_process_runner` | Asynchronous line-by-line stdout/stderr streaming, phase parsing, and graceful termination. |
| `test_live_transaction_execution` | Integration test with a simulated slow `prt-get`: verifies execution > 30s without timeouts, live line streaming, graceful `SIGTERM` cancellation without orphans, and single-command batching. |

---

## Governance & Collaboration

Development is governed by the working agreement in [`AGENTS.md`](AGENTS.md). All architectural decisions, empirical findings, and session logs are recorded chronologically in [`NOTES.md`](NOTES.md).
