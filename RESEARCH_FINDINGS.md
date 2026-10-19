# OctoCRUX — Research Findings

**Status:** Completed  
**Review Date:** October 19, 2026  
**Scope:** Read-Only Audit of Octopi, OctoPkg, OctoXBPS, YAPO, and Native CRUX Package/Ports Tooling.

---

## 1. Executive Summary

This document presents the source-code audit and architectural findings required by [RESEARCH.md](RESEARCH.md).

Key findings:
1. **Absence of Backend Abstraction in the Octo Family:** None of the Octo-family applications (`octopi`, `octopkg`, `octoxbps`) possess an abstract backend boundary. Instead, `octopkg` and `octoxbps` were created through copy-paste forks of `octopi`. They leak low-level package-manager commands, shell composition strings, and terminal environment hacks directly into UI classes (`MainWindow`, `UnixCommand`, `Package`).
2. **Technical Debt and Unsafe Practices:** The existing Octo codebases employ brittle practices:
   - Command composition via raw string concatenation rather than explicit argument vectors.
   - Unsafe cancellation via `killall pacman` or `killall pkg`.
   - Ad-hoc terminal width overrides (`COLUMNS=170`) to prevent stdout line wrapping.
   - Hardcoded privilege escalation paths (`/usr/local/bin/qt-sudo`).
3. **Zero Test Coverage:** None of the four candidate projects contain automated tests (0% unit, integration, or UI test coverage).
4. **Structured Capabilities of CRUX Tooling:** While CRUX is a minimalist source-based distribution, `prt-get printf` provides dedicated, tokenized, machine-readable output (`%n`, `%v`, `%r`, `%i`, `%p`, `%d`, etc.) specifically designed for tooling. This eliminates the need for fragile regex or column-splitting scrapers.
5. **YAPO's Proven CRUX Integration Model:** YAPO demonstrates effective CRUX integration patterns: using `prt-get printf` with explicit delimiters, reading `Pkgfile`, `README`, and `.footprint` directly from the filesystem path returned by `prt-get path`, and avoiding known stdout traps (such as diagnostic pollution from `prt-get --cache`).

---

## 2. Upstream Source Audit & Metadata

| Project | Upstream Repository URL | Inactive/Active | Checked Commit SHA | Commit Date | License | Build System | Qt Version Target | Key Dependencies |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Octopi** | [https://github.com/aarnt/octopi](https://github.com/aarnt/octopi) | Active | `ba83dfd50558e1c396363ddf7fd9ac7d68d303ad` | 2026-10-18 | **GPL-2.0** | CMake (0.20.0) / qmake | Qt 5 & Qt 6 | `libalpm`, `alpm_octopi_utils`, `qtermwidget5`/`6`, `glib-2.0` |
| **OctoPkg** | [https://github.com/aarnt/octopkg](https://github.com/aarnt/octopkg) | Semi-Active | `92aa76bfcaf2a4e68312dce1f36300fe5ff791ea` | 2026-06-07 | **LGPL-2.1** | qmake only | Qt 5 & Qt 6 | FreeBSD `pkg`, `qt-sudo` |
| **OctoXBPS** | [https://github.com/aarnt/octoxbps](https://github.com/aarnt/octoxbps) | Stalled | `5f1e8b200741df26b94405fe2de5feb06b6b9147` | 2025-09-07 | **GPL-2.0** | qmake only | Qt 6 (`core5compat`) | Void `xbps-*`, `qtermwidget6`, `qt-sudo` |
| **YAPO** | [https://github.com/fjaell/yapo](https://github.com/fjaell/yapo) | Stable/Dormant | `a230e7384b9b359ab9dea142f24b1a56f5cd1560` | 2025-12-13 | **GPL-2.0-or-later** | Makefile (install) | N/A (Ruby/ncurses) | CRUX `prt-get`, `pkgutils`, `ports`, `ncurses-ruby` |

---

## 3. Candidate Architectural Breakdown

### 3.1 Octopi
- **Architecture:** Octopi was designed specifically for Arch Linux. Its data layer relies on `libalpm` via a helper library (`alpm_octopi_utils`).
- **Backend Class:** `AlpmBackend` (`src/alpmbackend.h`, `src/alpmbackend.cpp`). Despite its name, this is a concrete class with static helper methods calling `alpm_*` C functions. It does not implement a polymophic or pluggable backend interface.
- **Subprocess & UI:** Transactions and AUR operations invoke `pacman` and AUR helpers (`yay`, `paru`, etc.) via `QProcess` or through `qtermwidget` in an embedded tab.
- **Coupling:** Arch-specific assumptions (ALPM databases, sync DB download, AUR voting, Arch Linux mirror lists) are pervasive across `MainWindow`, `OptionsDialog`, and `SettingsManager`.

### 3.2 OctoPkg
- **Architecture:** Forked from Octopi for FreeBSD's `pkgng` (`pkg(8)`).
- **Backend Model:** No backend class abstraction exists. Packaging queries are performed via `UnixCommand` (`src/unixcommand.cpp`) executing `pkg query` or `pkg search` as child processes.
- **Coupling & Leakage:**
  - `MainWindow` directly orchestrates transaction strings and invokes `pkg update -f`.
  - Process cancellation is implemented via `killall pkg` run through `doas`/`qt-sudo`.
  - Does not support source compilation or ports trees; it assumes pre-compiled binary packages.

### 3.3 OctoXBPS
- **Architecture:** Forked from Octopi/OctoPkg for Void Linux's `xbps`.
- **Backend Model:** Uses `XBPSExec` (`src/xbpsexec.cpp`) and `UnixCommand` (`src/unixcommand.cpp`). Neither implements an abstract interface.
- **Coupling & Leftover Code:**
  - Retains vestigial variables named `pacman` inside `UnixCommand` (`src/unixcommand.cpp:133`, `166`).
  - Contains orphaned calls to FreeBSD `pkg`: `pacman.start("pkg", args);` (`src/unixcommand.cpp:139`).
  - Comments repeatedly refer to "Pacman packages" and "AUR packages" (`src/globals.cpp:66`, `106`).
  - Transaction lock clearing function retains Arch naming: `doRemovePacmanLockFile()` (`src/mainwindow_transaction.cpp:1162`).
  - Package models scrape space-delimited text from `xbps-query -l` and `xbps-query -Rs` (`src/package.cpp:428-456`).

### 3.4 YAPO (Yet Another Ports Overview)
- **Architecture:** Single-file Ruby script (`yapo.rb`, 1147 lines) with an ncurses interface, created by CRUX core developer Juergen Daubert.
- **Design:** Cleanly encapsulates ports access in a `PrtGet` class (`yapo.rb:575-725`).
- **Command Invocation:**
  - Queries ports using structured `prt-get printf` format strings.
  - Queries dependencies using `prt-get quickdep <port>` and `prt-get depends <port>`.
  - Queries reverse dependencies using `prt-get dependent --all <port>`.
  - Reads metadata (`Pkgfile`, `README`, `.footprint`) directly from disk using `prt-get path <port>`.
- **Execution & Privileges:** Leaves build execution to `system(cmd)` after dropping ncurses terminal mode, giving the user direct interactive console access during `pkgmk` builds. All commands can be customized via `/etc/yapo.conf` (enabling `sudo`, `doas`, or custom wrapper execution).

---

## 4. Package Manager Leakage & Architectural Debt in Octo Codebases

The audit revealed extensive coupling where distribution-specific commands and brittle parsing leak into the UI and data models:

### 4.1 Shell String Composition & Unsafe Interpolation
Instead of constructing explicit argument lists (`QStringList`), OctoXBPS constructs command strings through naive string concatenation:
- **`octoxbps/src/xbpsexec.cpp:577`**:
  ```cpp
  QString command = UnixCommand::getXBPSInstallBin() + " -y " + listOfPackages;
  ```
- **`octoxbps/src/xbpsexec.cpp:642`**:
  ```cpp
  QString command = UnixCommand::getXBPSRemoveBin() + " -R -y " + listOfPackages;
  ```
- **`octoxbps/src/unixcommand.cpp:598-601`**:
  ```cpp
  sl = pCommand.split(QStringLiteral(" "), Qt::SkipEmptyParts);
  sl.insert(0, ctn_OCTOXBPS_SUDO_PARAMS);
  m_process->start(WMHelper::getSUCommand(), sl);
  ```
  *Risk:* Splitting on spaces breaks when arguments or paths contain whitespace or special characters.

### 4.2 Terminal Environment Overrides
- **`octoxbps/src/unixcommand.cpp:156-157`**:
  ```cpp
  env.remove("COLUMNS");
  env.insert("COLUMNS", "170");
  ```
  *Risk:* Hardcoding `COLUMNS=170` is a workaround for `xbps-query` terminal width truncations. It demonstrates tight coupling between the CLI tool's presentation layer and the application data layer.

### 4.3 Brittle Output Parsing in Data Models
- **`octoxbps/src/package.cpp:438-456`**:
  ```cpp
  QStringList parts = packageTuple.split(' ');
  pkgAux = parts[1];
  int dash = pkgAux.lastIndexOf("-");
  ...
  if (parts[0] == "[*]" || parts[0] == "i" || parts[0] == "ii")
  ```
  *Risk:* Tokenizing CLI output by spaces fails when descriptions or version strings contain irregular whitespace or unexpected characters.
- **`octoxbps/src/package.cpp:338-345`**:
  ```cpp
  int pos = infoTuple.indexOf("remove");
  ```
  *Risk:* Inspecting transaction removal output by searching for the arbitrary word `"remove"` within text lines.

### 4.4 Unsafe Process Termination
- **`octopkg/src/unixcommand.cpp:125-127`**:
  ```cpp
  sl << ctn_OCTOPKG_DOAS_PARAMS;
  sl << "killall";
  sl << "pkg";
  ```
- **`octopi/src/unixcommand.cpp:1212`**:
  ```cpp
  QString pCommand = QLatin1String("killall pacman; rm ") + ctn_PACMAN_DATABASE_LOCK_FILE;
  ```
  *Risk:* Running `killall <process>` is dangerous. It kills unrelated package-manager processes run by other users or automated scripts on the same machine. Proper cancellation requires sending signals (`SIGINT` / `SIGTERM`) to the specific child process group (`kill(-pgid, SIGINT)`).

### 4.5 Hardcoded Privilege Elevation Paths
- **`octoxbps/src/constants.h:106`**:
  ```cpp
  const QString ctn_OCTOXBPS_SUDO(QStringLiteral("/usr/local/bin/qt-sudo"));
  const QString ctn_OCTOXBPS_SUDO_PARAMS(QStringLiteral("-d"));
  ```
- **`octopkg/src/constants.h:12-13`**:
  ```cpp
  const QString ctn_OCTOPKG_DOAS = QStringLiteral("/usr/local/bin/qt-sudo");
  const QString ctn_OCTOPKG_DOAS_PARAMS = QStringLiteral("-d");
  ```
  *Risk:* Assumes `/usr/local/bin/qt-sudo` exists. CRUX installations do not have `qt-sudo` installed by default. Systems may use `sudo`, `doas`, or standard `su`.

---

## 5. CRUX Native Tooling & Integration Model

### 5.1 Native CRUX Command Reference

| CRUX Operation | Native Command | Purpose | Output Characteristics / Flags | Privilege Required |
| :--- | :--- | :--- | :--- | :--- |
| **Ports Query** | `prt-get printf <format>` | Query all ports metadata in tree | Tokenized output (e.g. `%n\t%v\t%r\t%i\t%l\t%p\t%d\t%R\t%e\n`) | None (Read-only) |
| **Installed Query** | `pkginfo -i` | List installed packages | Line format: `<pkgname> <version>-<release>` | None (Read-only) |
| **Package Files** | `pkginfo -l <pkg>` | List files installed by package | Absolute path per line | None (Read-only) |
| **File Owner** | `pkginfo -o <file>` | Find which package owns file | Header `Package  File`, paths without leading `/` | None (Read-only) |
| **Port Path** | `prt-get path <port>` | Locate port directory | Single absolute directory path (e.g. `/usr/ports/core/glibc`) | None (Read-only) |
| **Dependencies** | `printf %e` | Direct declared dependencies | Comma-separated list without spaces (e.g. `dep1,dep2`) | None (Read-only) |
| **Install Order** | `prt-get quickdep <port>` | Full recursive install order | Space-separated list ending with port itself, leading/trailing spaces | None (Read-only) |
| **Depends View** | `prt-get depends <port>` | Human-readable dependency tree | Formatted with `[i]` tags and `(provided by <alias>)` notes | None (Read-only) |
| **Removal Impact** | `prt-get dependent <port>` | Direct installed dependents | One port name per line (empty if none; rc=0 on nonexistent port) | None (Read-only) |
| **Reverse Deps** | `prt-get dependent --all <port>` | All ports declaring `<port>` | One port name per line (includes uninstalled ports) | None (Read-only) |
| **Version Diff** | `printf %i` & `%l` | Outdated packages detection | Evaluated in data model (`prt-get diff` hides locked packages) | None (Read-only) |
| **Locked Query** | `prt-get listlocked` / `%l` | List locked ports | Stored in `/var/lib/pkg/prt-get.locker`, token `%l` yields `yes`/`no` | None (Read-only) |
| **Aliases** | `/etc/prt-get.aliases` | Virtual / alternate package mapping | Direct file parse (e.g. `rust-bin: rust`, `jdk8-bin: openjdk8`) | None (Read-only) |
| **Port Sync** | `ports -u` | Sync ports collections from remote | Progress text from rsync/httpup/git | Root (`sudo` / `doas` / `qt-sudo`) |
| **Port Install** | `prt-get depinst <port>` | Compile & install port with deps | Long-running compiler stdout/stderr (`pkgmk`) | Root (`sudo` / `doas` / `qt-sudo`) |
| **Port Update** | `prt-get update <port>` | Compile & update package | Long-running compiler stdout/stderr | Root (`sudo` / `doas` / `qt-sudo`) |
| **Port Remove** | `prt-get remove <port>` | Remove installed package (`pkgrm`) | Output from `pkgrm` | Root (`sudo` / `doas` / `qt-sudo`) |
| **Port Lock/Unlock** | `prt-get lock/unlock <port>` | Prevent/allow automatic updates | Updates `/var/lib/pkg/prt-get.locker` | Root (`sudo` / `doas` / `qt-sudo`) |

### 5.2 Structured Querying via `prt-get printf` & Three-Axis Data Model
`prt-get printf` supports machine-readable formatting tokens:
- `%n` : Port Name
- `%v` : Version
- `%r` : Release
- `%i` : Installation status (`yes` = installed & up to date; `diff` = installed, but tree version differs; `no` = not installed)
- `%l` : Lock status (`yes` = locked in `/var/lib/pkg/prt-get.locker`; `no` = unlocked)
- `%p` : Filesystem Path to port directory
- `%d` : Description
- `%e` : Declared dependencies (comma-separated without spaces)
- `%R` : README indicator (`yes` / `no`)
- `%P` : Packager name (verified empty across all ports in CRUX 3.8; unusable)
- `%M` : Maintainer name
- `%E` : Pre-install script indicator (`yes` / `no`)
- `%O` : Post-install script indicator (`yes` / `no`)

*Query command:*
```bash
prt-get printf "%n\t%v\t%r\t%i\t%l\t%p\t%d\t%R\t%e\n"
```
*Benchmarked Performance:* Completed across all 2,307 ports in **69 ms** without cache on a ThinkPad T480s (SSD). Delimiters (`\t`) are strictly preserved without spurious line splitting.

#### Three-Axis Orthogonal Model
Port states must be classified across three orthogonal axes:
1. **Installation Axis (`%i`):** `yes`, `no`, `diff`.
2. **Lock Axis (`%l`):** `yes`, `no`.
3. **Alias Axis:** Mapped from `/etc/prt-get.aliases`. A port with `%i=no` (e.g. `rust`) is not missing if satisfied by an installed alias provider (`rust-bin`).

The 6 fundamental states of `%i` × `%l`:
- `yes` + `no` = Installed and up to date.
- `diff` + `no` = Outdated (eligible for sysup).
- `diff` + `yes` = Outdated, but Locked (suppressed by CLI `diff`/`quickdiff`, but surfaced by OctoCRUX).
- `yes` + `yes` = Installed, up to date, and locked.
- `no` + `no` = Available (not installed).
- `no` + `yes` = Locked uninstalled (prevents accidental compilation from source when a binary port is preferred).

### 5.3 Verified Lessons, Edge Cases & Parser Requirements
1. **`prt-get diff` & `quickdiff` Blindness:**
   Both `diff` and `quickdiff` omit locked packages, returning `"No differences found"` (rc=0) even when locked packages are outdated. Automated diffs must rely on `printf %i` combined with `%l`.
2. **Dependency Resolution Quirks:**
   - Direct dependencies: Obtained from `printf %e` (comma-separated list).
   - Recursive installation order: `prt-get quickdep <port>` returns the entire order ending with `<port>`, with leading and trailing whitespace (`" boehm-gc paper a2ps  "`).
   - Removal impact: `prt-get dependent <port>` (without `--all`) returns only direct installed dependents.
   - Nonexistent ports: `prt-get dependent <nonexistent>` returns rc=0 and empty stdout. The backend must validate port existence prior to querying.
   - `prt-get isinst`: Returns rc=255 for both uninstalled and error; do not use.
3. **Footprints, Integrity & Package Artifacts:**
   - CRUX footprints only record `NEW` and `MISSING` files.
   - `pkgmk -in`: Ignores only `NEW` files, providing a narrower and safer override than `-if` (which ignores all discrepancies).
   - Built package format: `/usr/ports/packages/<name>#<version>-<release>.pkg.tar.zst` (Zstandard compression).
   - Rejected configurations: `/var/lib/pkg/rejected` holds files from `pkgadd` updates awaiting `rejmerge`.
   - Failure logs: With `writelog enabled` and `rmlog_on_success yes`, failed builds leave `/var/log/pkgbuild/%n.log` during `sysup`.
4. **Direct File Inspection:**
   Metadata files (`Pkgfile`, `README`, `.footprint`, `pre-install`, `post-install`) should be read directly from the filesystem path returned by `prt-get path <port>` using `QFile`.

---

## 6. Subprocess, Terminal & Privilege Escalation Models

### 6.1 Subprocess Streaming vs Terminal Emulation
CRUX package operations require streaming live compiler output and interactive capability:
1. **Option A: Embedded Terminal Widget (`QTermWidget6`)**
   - *Advantages:* Provides a full pseudo-terminal (PTY) with terminal escape sequences, ANSI colors, interactive user prompts (e.g. interactive `Pkgfile` queries), and native `Ctrl+C` signal propagation.
   - *Disadvantages:* Introduces an external dependency (`qtermwidget6`).
2. **Option B: Headless `QProcess` with Log Streaming & External Terminal Option**
   - *Advantages:* Pure Qt dependency without third-party terminal libraries. Streams stdout/stderr line-by-line via `readyReadStandardOutput()` / `readyReadStandardError()`.
   - *Disadvantages:* Cannot handle interactive prompts without dedicated PTY handling.
3. **Recommended Hybrid Architecture:**
   - Execute read-only queries (`prt-get printf`, `pkginfo`, `prt-get diff`) asynchronously via `QProcess` with structured argument vectors.
   - Execute mutation transactions (`prt-get depinst`, `prt-get update`, `ports -u`) through an adapter capable of streaming to either an embedded PTY widget (`QTermWidget6` if available) or launching a configured external terminal emulator.

### 6.2 Process Tree Management & Safe Cancellation
- Killing a build operation must terminate `prt-get`, `pkgmk`, the compiler (`gcc`/`clang`), and any active configure scripts.
- Sending `SIGINT` or `SIGTERM` to the process group (`setpgid(0, 0)` and `kill(-pgid, SIGINT)`) ensures clean teardown.
- Never call `killall` or remove lock files globally.

### 6.3 Privilege Escalation on CRUX
CRUX does not install `systemd` or `polkit` by default. It utilizes standard Unix user permissions:
- Users configure `sudo`, `doas`, or `su`.
- OctoCRUX must not hardcode `/usr/local/bin/qt-sudo`.
- The privilege escalation mechanism must be auto-discovered (`sudo`, `doas`, `su`) and configurable in user settings, with explicit user prompting only when mutating the system.

---

## 7. License Compatibility & Attribution

All inspected codebases use compatible free software licenses:
- **Octopi:** GNU General Public License v2 (GPL-2.0) by Alexandre Albuquerque Arnt.
- **OctoPkg:** GNU Lesser General Public License v2.1 (LGPL-2.1) by Alexandre Albuquerque Arnt.
- **OctoXBPS:** GNU General Public License v2 (GPL-2.0) by Alexandre Albuquerque Arnt.
- **YAPO:** GNU General Public License v2 or later (GPL-2.0-or-later) by Juergen Daubert.

**Conclusion:** OctoCRUX can be licensed under **GPL-2.0** (or GPL-2.0-or-later), providing full compatibility with UI components borrowed from Octopi/OctoXBPS and concepts from YAPO, while maintaining appropriate copyright attribution.

---

## 8. Candidate Comparison Matrix

| Evaluation Dimension | Octopi | OctoPkg | OctoXBPS | YAPO | OctoCRUX Clean-Room Proposed |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **UI Maturity** | High (Polished Qt UI) | Medium (Octopi derivative) | Medium (Octopi derivative) | Low (ncurses text mode) | High (Modern Qt 6 widgets) |
| **Qt / Build Modernity** | CMake & qmake, Qt 5/6 | qmake only, Qt 5/6 | qmake only, Qt 6 | N/A (Ruby) | CMake, C++17, Qt 6 |
| **Backend Decoupling** | None (`libalpm` coupled) | None (`pkg` hardcoded) | None (`xbps` hardcoded) | Good (`PrtGet` class) | Strict (`Qt UI -> Adapter -> CRUX tools`) |
| **Subprocess Execution** | Mixed (`QProcess` + PTY) | Ad-hoc `QProcess` | Ad-hoc strings | `system(cmd)` in console | Structured `QProcess` + PTY streaming |
| **Command Output Parsing** | `libalpm` C API | Regex / spaces | Brittle string splitting | Structured `prt-get printf` | Tokenized `prt-get printf` |
| **Privilege Escalation** | `qt-sudo` / polkit | `qt-sudo` (`ctn_OCTOPKG_DOAS`) | Hardcoded `qt-sudo` | Configurable in `yapo.conf` | Configurable (`sudo`/`doas`/`su`) |
| **Terminal Integration** | Embedded QTermWidget | External terminal | Embedded QTermWidget6 | Native ncurses shell-drop | Modular PTY / External fallback |
| **Test Coverage** | 0% | 0% | 0% | 0% | Planned Unit & Integration Ladder |
| **License** | GPL-2.0 | LGPL-2.1 | GPL-2.0 | GPL-2.0+ | GPL-2.0 or GPL-2.0+ |
| **Maintenance Activity** | Active (Arch) | Low (FreeBSD) | Stagnant (Void) | Stable / Dormant (CRUX) | Active new project |
| **Adaptation Effort** | Very High (strip ALPM) | High (strip pkgng) | High (strip XBPS debt) | Rewrite (Ruby -> C++/Qt) | Predictable, modular design |
| **Blockers for Direct Fork** | Hardcoded ALPM bindings | Binary-only assumptions | High debt, broken cancel | Terminal-only, Ruby runtime | None |

