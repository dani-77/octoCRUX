# OctoCRUX — Development Log, Chronological Timeline & Technical Handoff

> **Document Purpose:**  
> This document provides a comprehensive chronological record of everything accomplished in the OctoCRUX project to date, including architectural choices, empirical findings, peer-review corrections (with Claude and ChatGPT), questions posed to the project owner, resolved decisions, and test verification results. It serves as an authoritative handoff document for AI collaborators (Claude, ChatGPT, Antigravity) and as project documentation history.

---

## 1. Project Vision & Governance

- **Objective:** Develop **OctoCRUX**, a native Qt 6 graphical frontend for CRUX Linux's package and ports management system (`prt-get`, `pkgutils`, `ports`), preserving the familiar Octopi/OctoXBPS desktop workflow while strictly respecting CRUX's source-based philosophy, minimalist design, and multi-collection overlay semantics.
- **Working Agreement ([`AGENTS.md`](AGENTS.md)):**
  - **Conversational Language:** European Portuguese (`pt-PT`) with the project owner (Daniel Azevedo).
  - **Technical Documentation, Code & Commits:** English.
  - **Empirical Grounding:** Treat observed behavior as evidence to investigate, not assertions to dismiss or blindly accept.
  - **Code Quality:** Modern, modular, maintainable C++17 / Qt 6 code; do not equate minimal line count with simplicity; no hardcoding of personal preferences or single-machine assumptions.
- **The SCOFS Protocol (*Special Case Oh Foda-Se*):**
  - Triggered whenever a test is green but fails to validate real-world behavior, or when observed behavior contradicts apparent test success.
  - Mandates: stop declaring success; produce minimal reproducers; add tests that exercise the right mechanism in real environments; record lessons in documentation.
  - **Rule:** Never claim tests passed unless executed; always report exact commands, environments, and outputs.

---

## 2. Chronological Timeline & Development Milestones

```
[Phase 0: Research & Audit]
  ├── Upstream Codebase Audits (Octopi, OctoPkg, OctoXBPS, YAPO)
  ├── Base Selection: Rejection of Direct Fork (Approach A) -> Selection of Reusable Hybrid Core (Approach B)
  └── Identification of Open Questions & Test Validation Ladder

[Empirical Diagnostics: CRUX 3.8 Bare Metal]
  ├── Execution of crux_diagnostics.sh (Run 1: 164451 — full tree scan, 2,307 ports)
  ├── Execution of crux_diagnostics.sh (Run 2: 164954 — gtk3 sample & dependents)
  └── Peer Review with Claude: Critique & Architectural Corrections (SCOFS Avoidance)

[Phase 1: Foundation, Interfaces & Parsers]
  ├── Specification of Canonical Three-Axis Domain Model (include/octocrux/types.h)
  ├── Abstract Backend Interface (include/octocrux/icruxbackend.h)
  ├── Step 3.1: PrintfParser & Real CRUX 3.8 Fixture Unit Test (2,307 ports)
  ├── Step 3.2: AliasesParser (/etc/prt-get.aliases) & DependencyParser (gtk3 sample)
  └── Step 3.3: CruxAdapter (Read-Only Backend) & ICommandRunner Mock Integration Tests

[Phase 2: Qt Models & Multi-Axis Filtering]
  ├── Step 4.1: PackageModel (QAbstractTableModel with O(1) index & 3-axis status formatting)
  └── Step 4.1: PackageFilterProxyModel (Multi-dimensional status, collection, and substring search)

[Level 2: Bare-Metal Validation & POSIX ERE Verification on T480s]
  ├── Execution of crux_capture_production.sh (Canonical 11-token printf: 2,307 ports, 0 errors, 68ms)
  ├── Empirical proof of pkginfo -o POSIX ERE regex semantics (prt.get matches, prt?get fails)
  ├── Implementation of CruxAdapter::escapeErePattern (escaping POSIX ERE metacharacters without breaking slashes/hyphens)
  └── Complete Verification: 5 test suites, 45 test functions, 100% passed in 0.11s

[Phase 3: Transaction Planner & Operations Layer]
  ├── Dependency Partitioning & Topological Build DAG (quickdep 118 -> 50+ satisfied)
  ├── Outdated Locked Package Detection (llvm, clang, lld, compiler-rt into lockedSkipped)
  ├── Multi-Tier Recursive Reverse Dependents Cascade (BFS cascade)
  └── Filesystem Inspection (raw byte reading of /proc/self/mountinfo & statvfs)

[Phase 4: Execution Engine, Privilege Helper & Live Streaming]
  ├── BuildLogAnalyzer (pkgmk 2-9 exit codes & ENOSPC/footprint/checksum/signature classifiers)
  ├── Dynamic PrivilegeHelper (qt-sudo -> sudo -> doas -> su discovery)
  ├── Headless CruxProcessRunner (asynchronous QProcess streaming, phase tracking, graceful kill)
  └── MainWindow Output Tab (monospaced streaming terminal, transaction actions & context menu)

[Milestone 11: Core Package Protection Policy & Fail-Closed Safety]
  ├── Complete elimination of hardcoded package lists (single source of truth)
  ├── Dynamic core collection path discovery via /etc/prt-get.conf (prtdir)
  ├── Direct disk membership check (<coreDir>/<name>) protecting overlay-overridden ports
  ├── Strict GUI removal block (ProtectedPackage error, 0 removal steps, fail-closed safety)
  └── Complete Verification: 10 test suites, 100% passed in 1.13s

[Milestone 12: SCOFS Protocol Resolution — Execution Engine, Batching & Live Streaming]
  ├── Root Cause: executeTransaction ran via synchronous DefaultCommandRunner (30s timeout, UI freeze)
  ├── Root Cause: discrete depinst steps triggered repeated qt-sudo elevation prompts per dependency
  ├── Root Cause: shell injection in su -c and unsafe SIGKILL watchdog orphaning root builds
  ├── Fix: CruxProcessRunner asynchronously wired to executeTransaction & syncPortsTree
  ├── Fix: single-invocation transaction batching (qt-sudo -d prt-get depinst <all targets>)
  ├── Fix: POSIX single-quote escaping in su -c; SIGKILL watchdog eliminated (SIGTERM only)
  └── Complete Verification: 11 test suites (including 36s real-process live test), 100% green in 37.38s

[Milestone 13: T480s Session (2026-10-20) — Execution Capture, Level 2 Re-check, Analyzer & Runner Fixes]
  ├── Capture: octocrux-exec-capture-20261020-214106 (pkgmk E_* codes, messages, prt-get --test)
  ├── Finding: pkgmk 5.40.12 defines E_FOOTPRINT/E_BUILD/E_INSTALL but exits 1 for them
  ├── Level 2: live check matches independent counts (2306 ports, 546 installed, 11 outdated, 5 locked)
  ├── Fix: BuildLogAnalyzer classifies from exact pkgmk lines; exit code values never mapped
  ├── Fix: child stdin closed (prompts get EOF); stalled-cancel warning, no SIGKILL; slow CTest label
  └── 5 fixes via fix/analyzer-and-runner + 2 review follow-ups, merged to main and pushed (904aceb); see SESSION_REPORT.md
```

---

## 3. Milestone Details & Historical Decisions

### 3.1 Milestone 1: Research, Audit & Base Selection
- **Document Artifacts:** [`RESEARCH_FINDINGS.md`](RESEARCH_FINDINGS.md), [`BASE_SELECTION.md`](BASE_SELECTION.md), [`OPEN_QUESTIONS.md`](OPEN_QUESTIONS.md).
- **Findings from Upstream Audits:**
  - *Octopi / OctoPkg / OctoXBPS:* Tightly coupled UI and package logic; extensive copy-paste technical debt across forks; unsafe shell command formatting via string concatenation (`QString("%1 %2").arg(...)`); broken cancellation mechanisms (Octopi relied on `killall pacman`, OctoPkg on `killall pkg`).
  - *YAPO (Yet Another Ports Overview):* Single-file Ruby script (`yapo.rb`, 1,147 lines) with an ncurses interface, created by CRUX core developer Juergen Daubert (GPL-2.0-or-later, last commit 2025-12-13). Demonstrates clean CRUX integration patterns (`prt-get printf`, direct reading of `Pkgfile`, `README`, `.footprint` from filesystem).
- **Settled Decision (Base Selection):**
  - **Approach B (Hybrid Strategy) adopted:** Build a clean-room Qt 6 / C++17 core and CRUX backend adapter from scratch, while reusing battle-tested UI layout files (`mainwindow.ui`, `transactiondialog.ui`) and icon resources from Octopi/OctoXBPS.

---

### 3.2 Milestone 2: Bare-Metal Diagnostics on CRUX 3.8
- **Tool Created:** [`crux_diagnostics.sh`](tools/crux_diagnostics.sh) (strictly read-only POSIX diagnostic script).
- **Runs Executed on ThinkPad T480s (CRUX 3.8, Linux 6.12.x, SSD):**
  - **Run 1 (`octocrux-diag-20261019-164451.tar.gz`):** Captured 540 files covering environment, mount points, `prt-get.conf`, `pkgmk.conf`, `pkgadd.conf`, and full `prt-get printf` query output of all 2,307 ports in 69 ms.
  - **Run 2 (`octocrux-diag-20261019-164954.tar.gz`):** Captured targeted dependency trees using `gtk3` as the sample port (`dependent`, `dependent --all`, `quickdep`, `pkginfo -l`).

---

### 3.3 Milestone 3: Peer Review with Claude & Architectural Corrections

Following the diagnostic runs, a thorough peer-review exchange between Daniel, Claude, and Antigravity identified critical distinctions that required architectural corrections in [`ARCHITECTURE.md`](ARCHITECTURE.md):

1. **`PKGMK_WORK_ROOT` Demoted from Global Assumption:**
   - *Initial Hypothesis:* OctoCRUX could pass `PKGMK_WORK_ROOT=/disk/path` on command invocation to redirect builds away from full `tmpfs`.
   - *Claude's Catch & Verification:* `sudo` clears the environment by default (`env_reset`), and Daniel's `/etc/sudoers` does not have `env_keep` for `PKGMK_WORK_ROOT`. Furthermore, `PKGMK_WORK_ROOT` was a custom addition in Daniel's `pkgmk.conf`, not standard CRUX.
   - *Resolution:* OctoCRUX does not assume `PKGMK_WORK_ROOT` exists. Build environment comes from `pkgmk.conf`. OctoCRUX evaluates `statvfs` on the active working directory to monitor available storage, while `/proc/self/mountinfo` supplies filesystem type (e.g. `tmpfs` vs disk) and mount options. Because CRUX provides no advance per-port build footprint estimation, OctoCRUX surfaces available space and filesystem type, and classifies out-of-space build failures (`ENOSPC`) post-mortem as `OperationErrorKind::DiskSpaceError`. (Note: 50 GiB is the owner's recommended fallback *disk partition* headroom target, not a static tmpfs threshold).
2. **Process Cancellation Demoted to Open Hypothesis (SCOFS Protocol):**
   - *Initial Hypothesis:* `qt-sudo` shares desktop UID, so sending signals to it was assumed to cleanly kill the build.
   - *Claude's Catch & Verification:* Sharing UID only means we can send signals to `qt-sudo`; it does not prove `qt-sudo` forwards `SIGINT`/`SIGTERM` to the child `sudo` process. If `qt-sudo` dies and leaves child root builds orphaned, declaring success would be a severe SCOFS failure.
   - *Resolution:* Cancellation remains an open hypothesis pending disposable container testing (Test 2.10.3). Pseudo-terminal (PTY) wrapper retained as the primary architectural safeguard.
3. **Authentication & `NOPASSWD: ALL` in Sudoers:**
   - *Observation & Clarification [owner-reported]:* While `sudo -n -l` indicated `(ALL) NOPASSWD: ALL`, the project owner explicitly reported that `sudo` prompts for a password in both the terminal and `qt-sudo` on this machine. `sudo -l` output does not reliably predict runtime authentication requirements, meaning authentication dialog tests and credential handling are valid and strictly necessary on this machine. (Previous speculations regarding graphical PAM configuration are retracted).
   - *Correction (2026-10-21) [owner-reported]:* the password prompt described above was seen on another of the owner's systems (most likely devXd77, Devuan), not on the T480s. On the T480s the `(ALL) NOPASSWD: ALL` rule is the last match, so sudo and qt-sudo do not ask for a password (OPEN_QUESTIONS 2.15). Testing the qt-sudo password dialog there requires commenting out that rule.
4. **PrivilegeHelper Dynamic Flags:**
   - *Resolution:* Eliminated hardcoded `-d` flag. `PrivilegeHelper` dynamically discovers backend capabilities (`qt-sudo`, `sudo`, `doas`, `su`) or reads user preferences.
5. **Digital Signature Failures (`signify`):**
   - *Observation:* Daniel's `/etc/pkgmk.conf` specifies `PKGMK_PRIVATEKEY="/home/dani77/.signify/d77crux.sec"`.
   - *Resolution:* Added `OperationErrorKind::SignatureError` to distinguish signature failures from compilation errors or footprint mismatches.
6. **`pkgmk` Exit Codes & `prt-get` Indirection:**
   - *Table from `man pkgmk(8)`:*
     | Exit Code | Meaning in `pkgmk` |
     | :--- | :--- |
     | `0` | No error occurred |
     | `1` | General error |
     | `2` | The Pkgfile is invalid |
     | `3` | Source or build directory is missing or lacking read/write permissions |
     | `4` | Error downloading source files |
     | `5` | Error unpacking source files |
     | `6` | md5sum mismatch |
     | `7` | Footprint mismatch |
     | `8` | Error running `build()` function |
     | `9` | Error installing package via `pkgadd` |
   - *Architectural Rule & Status:* `prt-get` does not propagate `pkgmk`'s specific 2–9 exit codes. While exit code `255` for nonexistent ports was empirically verified on CRUX, the exit code returned by `prt-get` upon build failure (e.g., 1 or other) has not yet been observed on bare metal (`man prt-get` states "to be determined") and is marked as **[hypothesis]**. Therefore, fine-grained error classification under `prt-get` builds must be extracted by parsing `/var/log/pkgbuild/<port>.log`. `OperationErrorKind` in `types.h` mirrors `pkgmk(8)`'s exact categories.

---

### 3.4 Milestone 4: Phase 1 — Foundation, Domain Model & Parsers (Level 1)

#### The Three-Axis Orthogonal Domain Model
Defined in [`include/octocrux/types.h`](include/octocrux/types.h):
- **Axis 1 (Installation Status):** From `prt-get %i` (`yes` $\rightarrow$ `Installed`, `no` $\rightarrow$ `NotInstalled`, `diff` $\rightarrow$ `Outdated`).
- **Axis 2 (Lock Status):** From `prt-get %l` (primary source) cross-checked against `/var/lib/pkg/prt-get.locker` (`yes` $\rightarrow$ `Locked`, `no` $\rightarrow$ `Unlocked`). Discrepancies between `%l` and the locker file indicate potential state drift to be flagged rather than resolved silently.
- **Axis 3 (Virtual Alias Provider):** From `/etc/prt-get.aliases` (e.g. `rust` provided by `rust-bin`). `%i=no` does not mean missing if an installed alias satisfies it.
- **Resulting 6 Fundamental States:**
  1. *Installed* (unlocked)
  2. *Installed & Locked*
  3. *Outdated* (eligible for routine update)
  4. *Outdated & Locked* (`llvm`, `clang`, `lld`, `compiler-rt` — silently hidden by `prt-get diff`!)
  5. *Available* (uninstalled, unlocked)
  6. *Locked Uninstalled* (`rust` — locked to prevent compiling from source when binary alias is preferred)

#### Abstract Backend Interface
Defined in [`include/octocrux/icruxbackend.h`](include/octocrux/icruxbackend.h): Pure abstract Qt interface (`ICruxBackend`) completely decoupled from widgets.

#### Level 1 Pure Parsers Implemented & Verified:
1. **[`PrintfParser`](src/core/printf_parser.h) / [`.cpp`](src/core/printf_parser.cpp):**
   - Canonical format string constants declared in header:
     - `PRODUCTION_FORMAT`: `"%n\t%v\t%r\t%i\t%l\t%p\t%R\t%E\t%O\t%e\t%d\n"` (11 tokens). Rationale: Description (`%d`) is placed strictly last so that any embedded tabs inside descriptions will not corrupt or shift the previous 10 structured fields. `%E` (pre-install) and `%O` (post-install) are captured to support pkgadd recovery path warnings.
     - `FIXTURE_FORMAT`: `"%n\t%v\t%r\t%i\t%p\t%d\t%R\t%e\n"` (8 tokens, matching diagnostic Run 1).
   - Strict schema enforcement: `Production11Token` strictly requires `tokenCount >= 11` (joining tokens 10+ for description); `Fixture8Token` strictly requires `tokenCount == 8`. Lines with unexpected token counts are rejected.
   - Extracts collection name from collection path (`/usr/ports/opt` $\rightarrow$ `"opt"`, `/usr/ports/d77crux` $\rightarrow$ `"d77crux"`).
   - Tested in [`tests/test_printf_parser.cpp`](tests/test_printf_parser.cpp): Parses all 2,307 ports from CRUX 3.8 fixture in **35 ms**; correctly identifies the 4 outdated locked packages; validates 11-token format with embedded tab in description; strictly rejects schema mismatches.
2. **[`AliasesParser`](src/core/aliases_parser.h) / [`.cpp`](src/core/aliases_parser.cpp):**
   - Parses `/etc/prt-get.aliases` (`<provider>: <virtual>`).
   - Bidirectional lookup (`virtualToProvider` and `providerToVirtual`).
   - Injects `providedByAlias` on virtual ports (e.g., `rust` marked with `providedByAlias = "rust-bin"`).
   - Tested in [`tests/test_aliases_parser.cpp`](tests/test_aliases_parser.cpp): 5 tests passed in 1 ms.
3. **[`DependencyParser`](src/core/dependency_parser.h) / [`.cpp`](src/core/dependency_parser.cpp):**
   - `parseDependents`: Parses `prt-get dependent` (7 installed direct dependents for `gtk3` — removal safety) and `prt-get dependent --all` (38 declared dependents for `gtk3`).
   - `parseQuickdep`: Parses space-delimited recursive installation order (118 packages for `gtk3`).
   - `resolveQuickdepWithAliases`: Bridges `quickdep` with the real system by substituting virtual package `rust` with concrete installed package `rust-bin`.
   - Tested in [`tests/test_dependency_parser.cpp`](tests/test_dependency_parser.cpp): 7 tests passed in 1 ms.

---

### 3.5 Milestone 5: Phase 1 — CruxAdapter (Read-Only Backend)
- **Files:** [`src/backend/command_runner.h`](src/backend/command_runner.h), [`src/backend/command_runner.cpp`](src/backend/command_runner.cpp), [`src/backend/crux_adapter.h`](src/backend/crux_adapter.h), [`src/backend/crux_adapter.cpp`](src/backend/crux_adapter.cpp).
- **Capabilities Implemented:**
  - `queryAllPorts()`: Runs `prt-get printf` with `PrintfParser::PRODUCTION_FORMAT`, reads `/var/lib/pkg/prt-get.locker` and `/etc/prt-get.aliases` from disk, indexes all ports in memory.
  - `queryPort(name)`: Fast lookup returning `std::optional<PortInfo>`.
  - `queryOutdatedPorts()` / `queryLockedPorts()` / `queryAliases()`.
  - `queryDirectInstalledDependents(port)` / `queryAllDeclaredDependents(port)`: Pre-checks port existence in catalog to mitigate the CRUX trap where nonexistent ports return exit code 0 with empty output.
  - `queryRecursiveInstallOrder(port)`: Runs `quickdep` and resolves virtual packages with installed aliases.
  - `readPortFile(port, filename)`: Directly reads port files (`Pkgfile`, `.footprint`, `README`).
  - `queryFilesForInstalledPackage(pkg)`: Invokes `pkginfo -l <pkg>` and formats absolute paths.
  - `findPackageOwnerOfFile(path)`: Invokes `pkginfo -o <pattern>`, normalizes root paths (strips leading slashes to match `pkginfo` relative paths), and iterates all candidate records for an exact match to prevent prefix or substring confusion. (Note: Whether `pkginfo -o` evaluates `<pattern>` via shell glob `fnmatch` or POSIX regex `regexec` is an open hypothesis pending Level 2 testing on CRUX bare metal; if regex, metacharacters like `+` in `/usr/bin/g++` will require pre-escaping).
- **Tested in [`tests/test_crux_adapter.cpp`](tests/test_crux_adapter.cpp):** 11 integration tests passed in 40 ms using `MockCommandRunner` and real fixtures (exercising post-filtering of multiple candidates).

---

### 3.6 Milestone 6: Phase 2 — Qt Models & Multi-Axis Filtering
- **Files:** [`src/model/package_model.h`](src/model/package_model.h), [`src/model/package_model.cpp`](src/model/package_model.cpp), [`src/model/package_filter_proxy_model.h`](src/model/package_filter_proxy_model.h), [`src/model/package_filter_proxy_model.cpp`](src/model/package_filter_proxy_model.cpp).
- **Features Implemented:**
  - `PackageModel` (`QAbstractTableModel`):
    - 5 columns: `Name`, `Version` (formatted as `version-release`), `Status`, `Collection`, `Description`.
    - Custom model roles: `PortNameRole`, `InstallStatusRole`, `LockStatusRole`, `ProvidedByAliasRole`, `CollectionRole`.
    - Rich HTML `ToolTipRole` summarizing port details and dependencies.
    - Status formatting: `Installed`, `Installed (Locked)`, `Outdated`, `Outdated (Locked)`, `Virtual (<provider>) [Locked]`, `Available`.
    - Multi-column in-memory sorting.
  - `PackageFilterProxyModel` (`QSortFilterProxyModel`):
    - `StatusFilter`: `All`, `Installed` (includes satisfied virtual aliases), `NotInstalled`, `Outdated`, `Locked`.
    - `CollectionFilter`: Filters by repository overlay (`core`, `opt`, `xorg`, `d77crux`).
    - Substring search: Fast case-insensitive search across name and optionally package description.
    - Clean Qt 6 implementation using non-deprecated `invalidate()` with zero compiler warnings.
- **Tested in [`tests/test_package_model.cpp`](tests/test_package_model.cpp):** 11 unit tests passed in 56 ms.

---

### 3.7 Milestone 7: Level 2 Bare-Metal Validation & POSIX ERE Escaping
- **Captures Received from T480s:** `octocrux-prod-capture-20261019-203243.tar.gz`, [`pkginfo_o_semantica.xlsx`](pkginfo_o_semantica.xlsx).
- **Key Empirical Verifications:**
  1. *Canonical 11-Token `prt-get printf` Fixture:* Full scan of 2,307 ports completed in **68 ms** with **0 field count errors**. Description (`%d`) placed strictly last completely avoids field shifting. The captured `.cmd` invocation `argv` matches `PrintfParser::PRODUCTION_FORMAT` verbatim.
  2. *Empirical `pkginfo -o` Pattern Semantics:* Definitively proven to use **POSIX Extended Regular Expressions (ERE)** via `regcomp`/`regexec`, not shell globbing (`fnmatch`).
     - Proof: `pkginfo -o usr/bin/prt.get` matched `prt-get` (regex `.`), while `pkginfo -o "usr/bin/prt?get"` returned no owner (glob `?` ignored).
     - Substring behavior: An escaped pattern like `usr/lib/libstdc\+\+\.so\.6` returns multiple candidate files (.so.6, .so.6.0.33, -gdb.py).
     - Anchoring failure: Anchors `^` and `$` fail on relative paths.
  3. *Implementation of [`CruxAdapter::escapeErePattern`](src/backend/crux_adapter.cpp):* Escapes all POSIX ERE metacharacters (`. [ \ ( ) * + ? { | ^ $`) without escaping `/` or `-` (which are literal in POSIX ERE and broken by Qt's PCRE escape function).
  4. *Post-Filtering:* [`CruxAdapter::findPackageOwnerOfFile`](src/backend/crux_adapter.cpp) post-filters candidate lines by requiring `candidateFile == normalizedTarget`.
- **Tested in [`tests/test_crux_adapter.cpp`](tests/test_crux_adapter.cpp) and [`tests/test_printf_parser.cpp`](tests/test_printf_parser.cpp):** 100% verified.

---

### 3.8 Milestone 8: Phase 2 Step 4.2 — Main Window UI (`MainWindow` & `main.cpp`)
- **Implemented Files:**
  - [`src/ui/mainwindow.h`](src/ui/mainwindow.h) / [`src/ui/mainwindow.cpp`](src/ui/mainwindow.cpp): Complete Qt 6 `QMainWindow` following the Octopi desktop workflow:
    - Top toolbar with exclusive status filter toolbuttons (*All*, *Installed*, *Outdated*, *Locked*, *Available*), collection dropdown (`All`, `core`, `opt`, `xorg`, `d77crux`), search box with real-time text filtering on package name and description, and refresh action.
    - Central vertical `QSplitter` hosting `QTableView` (connected to `PackageFilterProxyModel` and configured for full-row selection, alternating row colors, column auto-resizing, and sorting) and a bottom `QTabWidget` with 5 detail tabs:
      - *Information Tab:* Formatted HTML summary displaying name, version, release, collection, install status, lock status, path, declared dependencies, and install script flags (`%E`, `%O`).
      - *Files Tab:* Installed file list populated via `queryFilesForInstalledPackage(port)` with placeholder text for uninstalled packages.
      - *Pkgfile Tab:* Monospace read-only view of the port's build recipe via `readPortFile(port, "Pkgfile")`.
      - *Footprint Tab:* Monospace read-only view of the port's expected footprint via `readPortFile(port, ".footprint")`.
      - *Readme Tab:* Monospace read-only view of the port's `README` via `readPortFile(port, "README")` when `%R == yes`.
    - Status bar providing real-time package counts: total ports, installed count, outdated count, locked count, and currently filtered row count.
  - [`src/main.cpp`](src/main.cpp): Application entry point creating `QApplication`, initializing `CruxAdapter`, and presenting `MainWindow`.
  - [`CMakeLists.txt`](CMakeLists.txt): Added `Qt6::Widgets`, updated `octocrux_core` to include UI sources, and added the main `octocrux` executable target.
- **Tested in [`tests/test_mainwindow.cpp`](tests/test_mainwindow.cpp):** 7 offscreen unit tests verifying catalog loading, status button switching, collection filtering, live search filtering, and detail tabs metadata updates. All 7 tests passed in 91 ms.

---

### 3.9 Milestone 9: Phase 3 — Transaction Planner & Operations Layer
- **Implemented Files:**
  - [`include/octocrux/types.h`](include/octocrux/types.h): Added `TransactionAction` (`Install`, `Upgrade`, `Reinstall`, `Remove`, `Lock`, `Unlock`), `TransactionStep`, `FilesystemInfo`, and rich metadata in `TransactionPlan`.
  - [`src/operations/transaction_planner.h`](src/operations/transaction_planner.h) / [`src/operations/transaction_planner.cpp`](src/operations/transaction_planner.cpp): Operations planning engine enforcing CRUX invariants and SCOFS safeguards.
- **Empirical SCOFS Lessons and Concrete Safeguards:**
  1. *Linux `/proc` Zero-Size Quirk:* `/proc/self/mountinfo` reports `st_size = 0` via kernel `stat()`. `QTextStream::atEnd()` or `QFile::atEnd()` evaluates `pos() >= size()`, causing loops to exit before reading any lines. Resolved by reading raw bytes directly with `file.readAll()`, ensuring 100% reliable mount point and filesystem type (`tmpfs` vs disk) detection.
  2. *Topological Order vs. Action Partitioning:* `prt-get quickdep` outputs the full recursive dependency chain (e.g. 118 packages for `gtk3`). A naive planner would queue 118 builds. `TransactionPlanner` partitions the DAG into uninstalled packages (`toInstall`), outdated packages (`toUpdate`), and already-installed up-to-date packages (`satisfiedDependencies`, over 50 packages for `gtk3`).
  3. *Outdated Locked Package Handling:* `prt-get diff` silently hides locked packages (`llvm`, `clang`, `lld`, `compiler-rt`). `planUpgrade` catches all 4 outdated locked packages and puts them in `lockedSkipped` with explanatory warnings, preventing broken unprompted builds.
  4. *Locked Uninstalled Package Protection:* Explicitly locked uninstalled ports (such as `rust` in `opt` on the T480s to prevent accidental source compilation) are detected and blocked from installation with an error, unless satisfied by an installed virtual alias provider (`rust-bin`).
  5. *Multi-Tier Recursive Reverse Dependents Cascade:* Standard `prt-get dependent` only checks direct 1-level dependents. `calculateRecursiveDependentsCascade` executes a BFS cascade to compute the complete chain of broken installed packages (e.g. removing `gtk3` breaks `libnotify`, which in turn breaks `notification-daemon`).
  6. *Critical System Package Safeguards (Superseded by Milestone 11):* Previously used a hardcoded list of 19 packages; superseded in Milestone 11 by the authoritative Core Package Protection Policy checking disk presence in the core collection.
- **Tested in [`tests/test_transaction_planner.cpp`](tests/test_transaction_planner.cpp):** 16 comprehensive unit tests covering all edge cases, mount inspection, SCOFS scenarios, and core package protection rules (100% green in 39 ms).

---

### 3.10 Milestone 10: Phase 4 — Execution Engine, Privilege Helper & Live UI Streaming
- **Implemented Files:**
  - [`src/core/build_log_analyzer.h`](src/core/build_log_analyzer.h) / [`src/core/build_log_analyzer.cpp`](src/core/build_log_analyzer.cpp): Pure diagnostic classifier mapping `pkgmk(8)` exit codes 2–9 and regex matching build failures (`ENOSPC` disk space, footprint mismatch, checksum mismatch, `signify` digital signatures, missing dependencies).
  - [`src/backend/privilege_helper.h`](src/backend/privilege_helper.h) / [`src/backend/privilege_helper.cpp`](src/backend/privilege_helper.cpp): Dynamic elevation helper discovery order: `qt-sudo` -> `sudo` -> `doas` -> `su` -> unprivileged (`None`), formatting commands dynamically with verified flags (e.g. `qt-sudo -d <command>`).
  - [`src/backend/crux_process_runner.h`](src/backend/crux_process_runner.h) / [`src/backend/crux_process_runner.cpp`](src/backend/crux_process_runner.cpp): Headless asynchronous `QProcess` runner with non-blocking line streaming, phase detection (`======>`), and two-stage termination (`SIGTERM` followed by a 3000ms `SIGKILL` watchdog).
  - [`src/backend/crux_adapter.h`](src/backend/crux_adapter.h) / [`src/backend/crux_adapter.cpp`](src/backend/crux_adapter.cpp): Execution methods `executeTransaction`, `syncPortsTree` (`ports -u`), `lockPort`, `unlockPort`, and post-transaction `/var/lib/pkg/rejected` inspection.
  - [`src/ui/mainwindow.h`](src/ui/mainwindow.h) / [`src/ui/mainwindow.cpp`](src/ui/mainwindow.cpp): Integrated 6th "Output" tab with monospaced console styling and autoscroll, transaction execution toolbar buttons, progress bar with compilation phases, and right-click context menu.
- **Tested in [`tests/test_build_log_analyzer.cpp`](tests/test_build_log_analyzer.cpp), [`tests/test_privilege_helper.cpp`](tests/test_privilege_helper.cpp), [`tests/test_crux_process_runner.cpp`](tests/test_crux_process_runner.cpp):** All 30 unit tests passed.

---

### 3.11 Milestone 11: Core Package Protection Policy & Fail-Closed Safety
- **Document Artifact:** [`OctoCRUX — Core package protection policy.md`](OctoCRUX%20%E2%80%94%20Core%20package%20protection%20policy.md) (authored by Daniel Azevedo, 2026-10-19).
- **Core Problem & Empirical Finding:**
  - Out of 126 core ports in CRUX 3.8, 68 have empty `%e` because they assume the core foundation is already present.
  - In the entire 2,307-port tree, only 1 port (`libxcrypt`) declares `glibc` in `%e`.
  - A heuristic counting dependents would erroneously treat `glibc` as virtually unused and safe to remove!
  - Hardcoded package lists (`criticalSystemPackages()`) are brittle, maintenance-heavy, and un-CRUX-like.
- **Core Policy Invariants Implemented:**
  1. **Strict GUI Removal Block:** OctoCRUX never removes a package belonging to the CRUX `core` collection. No typed-confirmation or override inside the GUI. The only escape hatch is outside OctoCRUX via `prt-get remove` or `pkgrm` from a terminal.
  2. **Dynamic Discovery (*Fail-Closed*):** `CruxAdapter::coreCollectionPath()` dynamically parses `prtdir` directives in `/etc/prt-get.conf` to discover the directory whose basename is `core`. If no `core` collection is configured, `TransactionPlanner::planRemove()` fails closed: all removals are blocked and an explanatory error is returned.
  3. **Disk-Based Check Against Overlay Masking:** If an overlay (e.g. `d77crux`) overrides a core package (e.g. `glibc`), `prt-get printf` reports `%R == d77crux`. Checking `port.collection == "core"` would silently forfeit protection! Protection is enforced by checking if `<coreCollectionPath>/<portName>` exists directly on disk via `ICruxBackend::isCorePackage(portName)`.
  4. **Single Source of Truth:** `criticalSystemPackages()` and `isCriticalSystemPackage()` were completely removed from `TransactionPlanner`.
  5. **Outcome & Message:** Blocked removals return `OperationErrorKind::ProtectedPackage`, populate `protectedPackagesBlocked`, add zero removal steps/argv to the plan, and emit the exact user-facing message required by policy.
  6. **UI Action Guarding:** In `MainWindow`, the Remove button and context menu item are disabled with a dedicated tooltip: `"Core packages cannot be removed in OctoCRUX"`.
- **Tested in [`tests/test_crux_adapter.cpp`](tests/test_crux_adapter.cpp) and [`tests/test_transaction_planner.cpp`](tests/test_transaction_planner.cpp):**
  - `testRemoveGlibcBlockedAsProtectedPackage`: Confirms removal blocked, exact message, zero steps.
  - `testRemoveNonCorePackageAllowed`: Confirms normal removal of non-core package (`gtk3`).
  - `testUpgradeCorePackageAllowed`: Confirms updates of outdated core packages proceed normally.
  - `testCorePackageOverriddenByOverlayStaysProtected`: Synthetic overlay `d77crux` overriding `glibc` maintains protection.
  - `testNoCoreCollectionConfiguredFailsClosed`: Config without `core` collection blocks all removals.

---

### 3.12 Milestone 12: SCOFS Protocol Resolution — Execution Engine Wiring, Single-Password Batching, Shell Sanitization, and 40s Live Streaming Verification
- **Trigger:** Code review by project owner (Daniel Azevedo) on commit `eddc0ab` identifying a severe SCOFS condition where unit tests passed 100% green against mocks while real-world execution on CRUX bare metal would fail.
- **Root Cause Analysis (The 4 Fatal Disconnects):**
  1. *Wrong Runner Invocation:* `CruxAdapter::executeTransaction` was executing steps via `m_runner->run(...)` (`DefaultCommandRunner`), leaving `CruxProcessRunner` completely disconnected (`m_processRunner` was null/unused). This produced:
     - 30-second default timeout killing any compilation taking > 30s.
     - Blocking UI freeze because `waitForFinished()` blocked the GUI thread.
     - Complete absence of live streaming in the "Output" tab (output was emitted only after process finish).
     - Cancel button was a no-op (cancelled `m_processRunner`, which was never started).
  2. *Password Prompt Multiplicity:* `TransactionPlanner` expanded `quickdep` into individual discrete `depinst` steps per package, each wrapped in `qt-sudo`. Installing `gtk3` on a fresh system would trigger dozens of graphical password prompts instead of a single prompt.
  3. *Privilege Escalation Vulnerabilities:* `su -c` under `PrivilegeHelper` used naive double quotes vulnerable to shell interpolation (`$`, `` ` ``, `\`, `;`).
  4. *Watchdog Orphan Risk:* `CruxProcessRunner` escalated to `SIGKILL` after 3000ms. Sending `SIGKILL` to `qt-sudo` or `sudo` causes the helper to vanish while the child compilation build continues running orphaned as root.
- **Engine Redesign & Implemented Invariants:**
  1. *Asynchronous Wiring:* `CruxAdapter::executeTransaction` and `syncPortsTree` now instantiate and route strictly through `CruxProcessRunner`. Process launch is non-blocking (`startProcess` returns immediately). Real-time stdout/stderr lines are emitted as `outputReceived` and compilation phases as `progressChanged`. `MainWindow` GUI never blocks.
  2. *Per-Action Transaction Batching:* `executeTransaction` reads only `plan.steps` (`plan.requestedTargets` is filled by the planner but not used by the executor). Steps are grouped by action and each non-empty group becomes **one** elevated `prt-get` invocation, run in this order: `update <targets>`, `depinst <targets>`, `update -fr <targets>`, `remove <targets>`, `lock <targets>`, `unlock <targets>`. The `depinst` group lists every uninstalled port from the planner's `quickdep` expansion, not just the requested target. A plan with one action type elevates once; a mixed plan (e.g. installing a port with an outdated dependency, i.e. `update` + `depinst`) elevates once **per group**. Whether each elevation shows its own `qt-sudo` dialog is **[open]**; the owner reports that `sudo` asks once per command **[owner-reported]**. *(Corrected 2026-10-20; the original text claimed `requestedTargets` drove a single invocation and that elevation happened exactly once per transaction.)*
  3. *Graceful Signal Termination (No SIGKILL):* Removed `m_killTimer` and `onKillTimeout()`. `cancel()` issues `m_process->terminate()` (`SIGTERM`), allowing elevation helpers and `prt-get` to trap the signal and clean up child builds cleanly.
  4. *POSIX Single-Quote Escaping for `su -c`:* Each command argument is wrapped in POSIX single quotes (`'...'`), with internal single quotes safely escaped as `'\''`, neutralizing shell expansion and command injection.
- **Verification via Real-Process Integration Test (`tests/test_live_transaction_execution.cpp`):**
  - Uses `tests/scripts/fake_slow_prtget.sh` with `SIGTERM` trap handling.
  - `testExecutionExceeding30Seconds`: Runs simulated build for 34 seconds (exceeding legacy 30s timeout by 4s). Proves that live output lines arrive continuously from second 1 to 34 without arbitrary timeouts and exits 0.
  - `testGracefulCancellation`: Cancels mid-stream at second 2. Proves that `SIGTERM` arrives at child process, which logs graceful cleanup and terminates promptly within milliseconds without `SIGKILL`.
  - `testSingleBatchedInvocation`: Verifies via log probe that multi-target plan executes as exactly one command line (`depinst pkg1 pkg2 pkg3`), eliminating multiple password prompts.

---

## 4. Record of Questions Posed to Owner & User Decisions

| # | Question / Topic | Rationale / Context | User Response / Decision |
|---|---|---|---|
| 1 | **Architecture Review & SCOFS Avoidance** | Did the owner agree with Claude's critique regarding `PKGMK_WORK_ROOT`, cancellation unproven status, sudoers `NOPASSWD: ALL`, and dynamic privilege flags? | **Approved ("sim")** — Architecture reformulated to prevent SCOFS; variables demoted from assumptions to runtime detections. |
| 2 | **Development Host Toolchain & Dependency Installation** | The development machine is running Void Linux with GCC 14.2.1/15, CMake 4.2.2. Qt 6 runtime was installed, but `qt6-base-devel` (headers, cmake files) was missing. Did the owner authorize running `sudo xbps-install -S qt6-base-devel`? | **Approved ("podes instalar")** — Package installed cleanly (34 packages configured). |
| 3 | **Diagnostic Archive 2 (`164954`) Availability** | Asked the owner to copy the second diagnostic tarball to the workspace when available. | **Notified by owner ("já está na maquina o ficheiro que pediste; usa-o bem")** — Archive extracted and verified. |
| 4 | **Scope of Diagnostic Archive 2** | Owner clarified that the second run confirmed the findings already documented in the `gtk3` test note. | **Confirmed ("ele só te deve dizer aquilo que o --gtk3 teste já dizia")** — Raw capture files integrated as definitive fixtures. |
| 5 | **Approval to Implement Step 3.2 (Aliases & Dependents)** | Requested explicit permission before coding `AliasesParser`, `DependencyParser`, and their unit tests. | **Approved ("sim")** — Implemented and 100% verified. |
| 6 | **Approval to Implement Step 3.3 (`CruxAdapter` Read Backend)** | Requested explicit permission before coding `CruxAdapter`, `ICommandRunner`, and integration tests. | **Approved ("sim")** — Implemented and 100% verified. |
| 7 | **Approval to Implement Phase 2 Step 4.1 (`PackageModel` & Proxy)** | Requested explicit permission before coding Qt Model/View classes and tests. | **Approved ("sim")** — Implemented and 100% verified. |
| 9 | **Approval to Implement Phase 2 Step 4.2 (`MainWindow` & `main.cpp`)** | Requested explicit permission before coding Qt 6 GUI (`MainWindow`, toolbar filters, detail dock tabs, application executable). | **Approved (\"sim\")** — Implemented and 100% verified across 52 test functions. |
| 10 | **Approval to Implement Phase 3 (Transaction Planner & SCOFS Verification)** | Requested explicit permission before coding `TransactionPlanner` and testing CRUX invariants and SCOFS safeguards. | **Approved (\"sim, avança para vermos como fica no SCOFS ok?\")** — Implemented and 100% verified across 64 test functions. |
| 11 | **Public Git Repository Initialization & Remote Push** | User instructed to create git repository and push all material to GitHub. | **Completed (\"para já cria um repo em https://github.com/dani-77/octoCRUX e coloca lá todo este material\")** — Initialized git on branch `main`, committed full project tree, and pushed to `https://github.com/dani-77/octoCRUX`. |
| 12 | **Core Package Protection Policy Implementation** | Implemented policy from owner document: strict GUI removal block for core collection packages, dynamic core directory discovery, disk-based check against overlay hiding, removal of hardcoded lists. | **Approved ("wó te dou trabalhinhos de merda, não é? desculpa mas as ideias vêm com demasiada velocidade. sim, aprovo")** — Implemented and 100% verified across 10 test suites. |
| 13 | **SCOFS Protocol Resolution: Execution Engine, Batching, & Live Integration Test** | Owner reported blocking disconnect in `executeTransaction` (30s timeout, UI freeze, per-package elevation, `su -c` shell escaping). Proposed 4-stage resolution order including simulated slow `prt-get` integration test. | **Approved ("sim...por favor!")** — Implemented and 100% verified across 11 test suites. |
| 14 | **Execution Engine Peer Review & Audit Hardening** | Addressed findings from two comprehensive audits: probed elevation helper in `main.cpp` (keeping adapter None by default), exact 7-equal regex for pkgmk (`=======>`), strict token validation in `PrintfParser`, dynamic `PKGMK_WORK_DIR` discovery with `/var/tmp`/`/tmp` fallbacks, strict plan subordination in `executeTransaction`, and batched Upgrade-first ordering. Phase 5 work safely stored on branch `wip/phase5`. | **Approved ("Com estas emendas, podes dar-lhe o aval.")** — Implemented and 100% verified across all test suites. |

---

## 5. Current Test Verification Evidence

All tests executed via `ctest` in the project build environment:

```text
Test project /home/dani77/Projectos/octoCRUX/build
      Start  1: test_printf_parser
 1/11 Test  #1: test_printf_parser ................   Passed    0.11 sec
      Start  2: test_aliases_parser
 2/11 Test  #2: test_aliases_parser ...............   Passed    0.08 sec
      Start  3: test_dependency_parser
 3/11 Test  #3: test_dependency_parser ............   Passed    0.08 sec
      Start  4: test_crux_adapter
 4/11 Test  #4: test_crux_adapter .................   Passed    0.10 sec
      Start  5: test_package_model
 5/11 Test  #5: test_package_model ................   Passed    0.10 sec
      Start  6: test_mainwindow
 6/11 Test  #6: test_mainwindow ...................   Passed    0.13 sec
      Start  7: test_transaction_planner
 7/11 Test  #7: test_transaction_planner ..........   Passed    0.10 sec
      Start  8: test_build_log_analyzer
 8/11 Test  #8: test_build_log_analyzer ...........   Passed    0.08 sec
      Start  9: test_privilege_helper
 9/11 Test  #9: test_privilege_helper .............   Passed    0.08 sec
      Start 10: test_crux_process_runner
10/11 Test #10: test_crux_process_runner ..........   Passed    0.09 sec
      Start 11: test_live_transaction_execution
11/11 Test #11: test_live_transaction_execution ...   Passed   38.26 sec

100% tests passed, 0 tests failed out of 11
Total Test time (real) = 39.21 sec
```

### Breakdown of Executed Test Suites (11 test suites, 106 passed test runs):
1. **`test_printf_parser` (11 test functions passed):**
   - `initTestCase`: Reads 2,307 ports from `printf-literaltab.out` using `Schema::Fixture8Token`.
   - `testParseAllCount`: Exact count of 2,307 ports.
   - `testSamplePorts`: Validates `a2ps`, `adwaita-icon-theme`, `acl`, `abiword` (descriptions, paths, dependencies, README flags).
   - `testOutdatedPorts`: Identifies exact 4 outdated ports (`clang`, `compiler-rt`, `lld`, `llvm`).
   - `testApplyLocksAndThreeAxes`: Applies 5 locks from `prtget-listlocked.out` and validates the 3-axis state.
   - `testSyntheticProduction11Token`: Validates canonical 11-token format (`%n\t%v\t%r\t%i\t%l\t%p\t%R\t%E\t%O\t%e\t%d`) and verifies that descriptions containing embedded tabs are preserved without corrupting preceding fields.
   - `testMalformedLines`: Checks empty, truncated, and invalid lines.
   - `testCollectionExtraction`: Tests `/usr/ports/core`, `/usr/ports/d77crux/`, etc.
   - `testFormatConstantsAndStrictSchema`: Verifies `PRODUCTION_FORMAT` and `FIXTURE_FORMAT` constants; verifies strict rejection of mismatched token counts.
   - `testRealProduction11TokenFixture`: End-to-end verification of bare-metal capture (`printf-production-11token.cmd` and `.out`): verifies exact `argv` provenance match, parses all 2,307 ports, checks outdated locked ports, and validates pre/post install scripts counts (39 pre-install, 76 post-install).
2. **`test_aliases_parser` (5 test functions passed):**
   - `testParseRealAliases`: Bidirectional lookup for `rust-bin: rust`, `jdk8-bin: openjdk8`, `librsvg-compat: librsvg`, `mariadb: mysql`.
   - `testApplyAliasesToPorts`: Virtual package satisfaction only when provider is installed.
   - `testSyntheticEdgeCases`: Comments, whitespace trimming, malformed lines.
3. **`test_dependency_parser` (7 test functions passed):**
   - `testParseDirectInstalledDependents`: Exactly 7 direct installed dependents for `gtk3`.
   - `testParseAllDeclaredDependents`: Exactly 38 declared dependents for `gtk3`.
   - `testParseQuickdepOrder`: Exactly 118 packages in recursive order, ending with `gtk3`.
   - `testResolveQuickdepWithAliases`: Replaces virtual `rust` with concrete `rust-bin`.
   - `testEmptyAndEdgeCases`: Empty streams and edge cases.
4. **`test_crux_adapter` (12 test functions passed):**
   - `testQueryAllPortsAndThreeAxes`: End-to-end catalog query using real `printf-production-11token.out` fixture with provenance check.
   - `testQueryPort`: Existing port lookup (`gtk3`) and nonexistent port handling.
   - `testQueryOutdatedAndLockedPorts`: Set checks for outdated and locked ports.
   - `testQueryAliases`: Virtual to provider mapping.
   - `testDependents`: Direct (7) and all (38) dependents, guarded against CRUX nonexistent port quirk.
   - `testQuickdepWithAliasesResolution`: Full recursive dependency order with alias substitution.
   - `testReadPortFile`: Real file reading of `Pkgfile` and `.footprint` from fixture root.
   - `testPkginfoFilesAndOwner`: `pkginfo -l` file listing; `pkginfo -o` owner extraction verifying POSIX ERE regex escaping (`escapeErePattern`) on `/usr/bin/g++` and exact-match post-filtering across multiple substring candidates on `/usr/lib/libstdc++.so.6`.
   - `testCorePackageDetection`: Verifies dynamic discovery of core collection path from `/etc/prt-get.conf` and disk existence check `<core>/<name>`.
   - `testMutationStubs`: Confirms mutation stubs safely return false or route to execution.
5. **`test_package_model` (11 test functions passed):**
   - `testModelStructure`: 2,307 rows, 5 columns, headers, O(1) port lookup.
   - `testDisplayDataAndThreeAxesFormatting`: Formatting of Installed, Outdated (Locked), Virtual [Locked], Available.
   - `testFilterAll` / `testFilterOutdated` / `testFilterLocked` / `testFilterInstalledAndVirtual`: Multi-dimensional filtering across domain axes.
   - `testCollectionFilter`: Overlay filtering (`d77crux`).
   - `testSearchFilter`: Substring search in name and description.
   - `testSorting`: In-memory sorting ascending/descending.
6. **`test_mainwindow` (8 test functions passed):**
   - `initTestCase`: Initializes headless `MainWindow` loaded with mock `CruxAdapter` containing production fixture data.
   - `testInitialPopulationAndCounts`: Verifies status bar labels and proxy model row count matches 2,307 ports.
   - `testStatusFilterButtons`: Verifies exclusive status button switching (*Installed*, *Outdated*, *Locked*, *Available*, *All*) and corresponding filtered row counts.
   - `testCollectionFilterCombo`: Verifies collection combo switching (e.g. `core`, `opt`, `d77crux`) filters the table view accurately.
   - `testSearchBoxFiltering`: Verifies real-time text input filtering on package names and descriptions.
   - `testPortSelectionUpdatesDetailTabs`: Selects a port row in `QTableView` and validates that the 5 detail tabs (*Information*, *Files*, *Pkgfile*, *Footprint*, *Readme*) update with the port's metadata and recipe files.
   - `testCorePackageRemoveButtonDisabled`: Selects a core package (`glibc`) and verifies that the Remove button and context menu item are disabled with the tooltip `"Core packages cannot be removed in OctoCRUX"`.
   - `cleanupTestCase`: Cleans up GUI resources cleanly.
7. **`test_transaction_planner` (16 test functions passed):**
   - `initTestCase`: Initializes `CruxAdapter` with production fixtures and `inst-quickdep.out`/`inst-dependent.out`.
   - `testInstallPlanSimplePort`: Tests `a2ps` uninstalled target and verifies topological build ordering (`boehm-gc` -> `paper` -> `a2ps`); validates `acl` where both `acl` and `attr` are already satisfied.
   - `testInstallPlanDependenciesPartitioning`: Validates `gtk3` install plan: verifies that the 118 packages in quickdep are partitioned into actions vs satisfied dependencies (>50 already satisfied), preventing redundant build queues.
   - `testInstallPlanLockedUninstalledError`: Verifies that attempting to compile an explicitly locked uninstalled port (`rust` in `opt`) produces an invalid plan with an explanatory error.
   - `testUpgradePlanAllOutdatedWithLocks`: Verifies that `planUpgrade()` detects all 4 outdated locked packages (`llvm`, `clang`, `lld`, `compiler-rt`), does not queue them in `toUpdate`, places them in `lockedSkipped` with warnings, leaves 0 steps in the plan, and executing the plan finishes cleanly with 0 commands executed.
   - `testRemovalPlanDirectDependents`: Verifies that removing `gtk3` detects exactly the 7 installed direct dependents (`tint2`, `granite`, `firefox-bin`, `lxappearance`, `thunderbird-bin`, `glade`, `libnotify`) and populates `criticalWarnings` to trigger confirmation.
   - `testRemovalPlanRecursiveCascade`: Verifies that multi-level reverse dependents are discovered recursively via BFS cascade (`libnotify` -> `notification-daemon`).
   - `testRemoveGlibcBlockedAsProtectedPackage`: Verifies that removing `glibc` produces an invalid plan with `ProtectedPackage`, no removal steps/argv, the exact policy message, and `executeTransaction()` immediately rejects it with `OperationErrorKind::ProtectedPackage`.
   - `testRemoveNonCorePackageAllowed`: Verifies that removing non-core packages (`gtk3`) plans normally.
   - `testUpgradeCorePackageAllowed`: Verifies that upgrading an outdated core package is permitted and planned normally.
   - `testCorePackageOverriddenByOverlayStaysProtected`: Verifies that when an overlay overrides `glibc`, disk presence in `core` still blocks removal.
   - `testNoCoreCollectionConfiguredFailsClosed`: Verifies that missing `core` in `/etc/prt-get.conf` fails closed and blocks all removals.
   - `testNonexistentPortHandling`: Verifies that nonexistent ports return invalid plans with informative error messages.
   - `testFilesystemMountInspection`: Verifies empirical `statvfs` byte calculation, work directory discovery from `/etc/pkgmk.conf` (`PKGMK_WORK_DIR`), and `/proc/self/mountinfo` parsing.
   - `testLockTogglePlan`: Verifies discrete step creation for locking and unlocking a port.
   - `cleanupTestCase`: Cleans up resources.
8. **`test_build_log_analyzer` (14 test functions passed):**
   - Exit code mappings for `pkgmk(8)` codes 2–9.
   - Regex classifications for `ENOSPC`, footprint mismatches, checksum errors, signify signature failures, and generic errors.
   - Error summary formatting and failing port extraction.
9. **`test_privilege_helper` (9 test functions passed):**
   - Helper discovery order (`qt-sudo`, `sudo`, `doas`, `su`, `None`).
   - Dynamic wrapper argument generation for all backends including `qt-sudo -d`.
10. **`test_crux_process_runner` (7 test functions passed):**
    - Asynchronous line streaming on stdout and stderr.
    - Compilation phase tracking with exact 7-equal regex (`^=======>\\s+(.*)$`).
    - Graceful process cancellation via `SIGTERM` (`m_process->terminate()`), strictly eliminating `SIGKILL`.
11. **`test_live_transaction_execution` (6 test functions passed):**
    - `initTestCase`: Validates existence and executable permissions of `tests/scripts/fake_slow_prtget.sh`.
    - `testExecutionExceeding30Seconds`: Runs simulated build for 34 seconds (surpassing legacy 30000ms timeout threshold by 4s), proving live line streaming without arbitrary timeouts and clean exit 0.
    - `testGracefulCancellation`: User cancellation at second 2 proves that `SIGTERM` reaches the child build process cleanly, terminating in milliseconds.
    - `testSingleBatchedInvocation`: Multi-target plan (`{"pkg1", "pkg2", "pkg3"}`) verified via log probe to invoke exactly one command line (`depinst pkg1 pkg2 pkg3`), preventing repeated elevation password prompts.
    - `testMixedUpgradeAndInstallOrdering`: Verified that mixed transactions execute `Upgrade` first (`update ...`), then `Install` (`depinst ...`), with each batched into a single command (requiring two sequential elevations).
    - `cleanupTestCase`: Cleans up test artifacts.

---

## 6. Current File Tree

```
octoCRUX/
├── AGENTS.md                                         # Project working agreement & protocols
├── ARCHITECTURE.md                                  # Architectural specification & domain model
├── BASE_SELECTION.md                                # Analysis & selection of Hybrid Approach B
├── CMakeLists.txt                                   # Top-level CMake configuration (C++17, Qt 6 Core & Widgets)
├── NOTES.md                                         # This document (temporal log & handoff)
├── "OctoCRUX — Core package protection policy.md"   # Authoritative core protection policy by Daniel Azevedo
├── OPEN_QUESTIONS.md                                # Unresolved questions & SCOFS tracking ladder
├── README.md                                        # Project overview
├── RESEARCH_FINDINGS.md                             # Upstream audit (Octopi, OctoPkg, OctoXBPS, YAPO)
├── include/
│   └── octocrux/
│       ├── types.h                                  # Domain models (PortInfo, TransactionPlan, OperationErrorKind::ProtectedPackage)
│       └── icruxbackend.h                           # Pure abstract backend interface (ICruxBackend, isCorePackage, coreCollectionPath)
├── src/
│   ├── backend/
│   │   ├── command_runner.h / .cpp                  # ICommandRunner abstraction & DefaultCommandRunner
│   │   ├── crux_adapter.h / .cpp                    # CruxAdapter implementing ICruxBackend
│   │   ├── crux_process_runner.h / .cpp             # Headless QProcess runner with live streaming & phase parsing
│   │   └── privilege_helper.h / .cpp                # Dynamic privilege escalation helper (qt-sudo, sudo, doas, su)
│   ├── core/
│   │   ├── printf_parser.h / .cpp                   # Level 1 prt-get printf parser (11-token canonical format)
│   │   ├── aliases_parser.h / .cpp                  # Level 1 /etc/prt-get.aliases parser
│   │   ├── dependency_parser.h / .cpp               # Level 1 dependent & quickdep parser
│   │   └── build_log_analyzer.h / .cpp              # Log analyzer & classifier for pkgmk exit codes & ENOSPC
│   ├── model/
│   │   ├── package_model.h / .cpp                   # QAbstractTableModel for ports catalog
│   │   └── package_filter_proxy_model.h / .cpp      # QSortFilterProxyModel for 3-axis filtering & search
│   ├── operations/
│   │   ├── transaction_planner.h / .cpp             # TransactionPlanner (core package protection, dependency partitioning, mounts)
│   ├── ui/
│   │   ├── mainwindow.h / .cpp                      # Main Qt 6 window (toolbar, table, 6 detail tabs including Output, status bar)
│   └── main.cpp                                     # Application entry point (octocrux executable)
├── tests/
│   ├── CMakeLists.txt                               # Test targets configuration (Qt6::Test)
│   ├── test_printf_parser.cpp                       # Unit tests for PrintfParser (11 tests)
│   ├── test_aliases_parser.cpp                      # Unit tests for AliasesParser (5 tests)
│   ├── test_dependency_parser.cpp                   # Unit tests for DependencyParser (7 tests)
│   ├── test_crux_adapter.cpp                        # Integration tests for CruxAdapter (12 tests)
│   ├── test_package_model.cpp                       # Unit tests for PackageModel & Proxy (11 tests)
│   ├── test_mainwindow.cpp                          # Unit tests for MainWindow GUI (8 tests)
│   ├── test_transaction_planner.cpp                 # Unit tests for TransactionPlanner (16 tests)
│   ├── test_build_log_analyzer.cpp                  # Unit tests for BuildLogAnalyzer (14 tests)
│   ├── test_privilege_helper.cpp                    # Unit tests for PrivilegeHelper (9 tests)
│   ├── test_crux_process_runner.cpp                 # Unit tests for CruxProcessRunner (7 tests)
│   ├── test_live_transaction_execution.cpp          # Integration tests for live execution, 30s+ runtime & batching (5 tests)
│   ├── scripts/
│   │   └── fake_slow_prtget.sh                      # Test executable script for slow build simulation & SIGTERM handling
│   └── fixtures/
│       ├── crux3.8/                                 # Run 1 diagnostic captures (540 files)
│       │   └── cmd/                                 # Real production fixtures with provenance (.cmd, .out, .err, .rc)
│       └── crux3.8_run2/                            # Run 2 diagnostic captures (gtk3 sample)
└── tools/
    ├── crux_capture_execution.sh                    # Read-only capture: pkgmk codes/messages, build logs, prt-get --test
    ├── crux_capture_production.sh                   # Provenance-preserving production capture script
    ├── crux_diagnostics.sh                          # Read-only POSIX CRUX 3.8 diagnostic script
    ├── gen_pkgmk_failure_logs.sh                    # Real pkgmk failure fixtures (unprivileged sandbox runs)
    └── octocrux_live_check.cpp                      # Standalone CLI tool for bare-metal CRUX validation
```

---

## 7. Next Steps Roadmap

### 7.1 Level 2 Bare-Metal Validation on T480s (Completed & Verified)
Level 2 validation executed on the bare-metal ThinkPad T480s (CRUX 3.8, pkgutils 5.40.12, prt-get 5.19.9):
- **Canonical 11-Token Production Fixture:** Successfully captured with full execution provenance (`printf-production-11token.cmd`/`.out`). All 2,307 ports validated with zero line errors; `argv` provenance matched `PrintfParser::PRODUCTION_FORMAT` verbatim.
- **`pkginfo -o` POSIX ERE Regex Mechanism:** Empirically proven and verified. `CruxAdapter::escapeErePattern` implemented and verified.
- **Fixture Integration & Unit Tests:** Captured fixtures imported into `tests/fixtures/crux3.8/cmd/`; all test functions passing.

### 7.2 Phase 2 Step 4.2 — Main Window UI (`MainWindow` & `main.cpp`) (Completed & Verified)
Constructed clean Qt 6 graphical interface adhering to Octopi desktop patterns and CRUX principles:
- Implemented `src/ui/mainwindow.h` / `src/ui/mainwindow.cpp`.
- Toolbar with search bar, collection combo, status filter buttons: *All*, *Installed*, *Outdated*, *Locked*, *Available*.
- Central `QTableView` hooked up to `PackageFilterProxyModel`.
- Bottom dock `QTabWidget` displaying real-time details (*Information*, *Files*, *Pkgfile*, *Footprint*, *Readme*).
- Application entry point `src/main.cpp` generating native `octocrux` binary.
- Unit test `tests/test_mainwindow.cpp` validating all components offscreen.

### 7.3 Phase 3 — Transaction Planner & Operations Layer (Completed & Verified)
Operations planning engine enforcing CRUX invariants and SCOFS safeguards:
- Implemented `include/octocrux/types.h` and `src/operations/transaction_planner.h / .cpp`.
- Dependency partitioning: separates uninstalled builds, updates, and satisfied dependencies.
- Virtual alias mapping (`rust` -> `rust-bin`).
- Outdated locked packages detection (`llvm`, `clang`, `lld`, `compiler-rt`) into `lockedSkipped`.
- Locked uninstalled ports safeguard (`rust` in `opt`).
- Multi-tier reverse dependents cascade.
- Empirical filesystem inspection via `statvfs` and raw byte reading of `/proc/self/mountinfo`.
- Unit test `tests/test_transaction_planner.cpp`.

### 7.4 Phase 4 — Execution Engine, Privilege Helper & Live UI Streaming (Completed & Verified)
Execution and privilege escalation engine fully implemented and connected to UI:
- **Build Log Analyzer & Classifier (`src/core/build_log_analyzer.h` / `.cpp`):**
  - Pure diagnostic classifier categorizing build and package errors into `OperationErrorKind`.
  - Mapped `pkgmk(8)` exit codes 2–9 (`PkgfileError`, `SourceDirError`, `DownloadError`, `UnpackError`, `ChecksumError`, `FootprintError`, `CompilationError`, `InstallError`).
  - Implemented prioritized regex matching for wrapper runs (`prt-get` generic exit 1) to identify `DiskSpaceError` (ENOSPC), `FootprintError`, `ChecksumError`, `SignatureError` (signify), and syntax errors.
  - Extracted failing port names and concise human-readable failure summaries.
- **Dynamic Privilege Escalation Helper (`src/backend/privilege_helper.h` / `.cpp`):**
  - Probes available elevation helpers in order of CRUX desktop preference: `qt-sudo` -> `sudo` -> `doas` -> `su` -> unprivileged (`None`).
  - Implemented exact command wrapping for each backend. Formatted `qt-sudo` invocation as `qt-sudo -d <command> <args...>` based on empirical bare-metal CRUX 3.8 fixture verification (`qt-sudo-help.out`).
- **Headless Asynchronous Execution Runner (`src/backend/crux_process_runner.h` / `.cpp`):**
  - Headless `QProcess` runner with non-blocking line-buffered streaming for `stdout` and `stderr`.
  - Emits `outputReceived(line, isError)` and parses `======> ...` phase headers into `phaseChanged(phase)`.
  - Graceful cancellation mechanism sending `SIGTERM` with escalation to `SIGKILL` after a 3000ms watchdog timer if the process does not terminate.
- **Backend Execution Adapter (`src/backend/crux_adapter.h` / `.cpp`):**
  - Implemented `executeTransaction(plan)` iterating through planned steps with progress updates.
  - Implemented `syncPortsTree()` (`ports -u`), `lockPort(name)`, and `unlockPort(name)`.
  - Implemented post-transaction inspection of `/var/lib/pkg/rejected` emitting `rejectedConfigsDetected`.
- **UI Integration in `MainWindow` (`src/ui/mainwindow.h` / `.cpp`):**
  - Added 6th detail tab **Output** in bottom pane with dark terminal styling (`#1e1e1e`) and auto-scrolling log streaming.
  - Toolbar transaction buttons: **Sync** (`ports -u`), **Install**, **Remove**, **Upgrade**, **Lock** / **Unlock**, and **Cancel**.
  - Status bar execution widgets: `QProgressBar` and `QLabel` displaying active compilation phase.
  - Custom right-click context menu on ports table (*Install*, *Remove*, *Upgrade*, *Lock/Unlock*, *Copy Port Name*).
  - Safety confirmations for critical system package removals and alert notifications for rejected config files.

### 7.5 Milestone 11 — Core Package Protection Policy & Fail-Closed Safety (Completed & Verified)
Authoritative protection policy implemented per [`OctoCRUX — Core package protection policy.md`](OctoCRUX%20%E2%80%94%20Core%20package%20protection%20policy.md):
- Completely eliminated hardcoded essential package lists (`criticalSystemPackages()`).
- Added `OperationErrorKind::ProtectedPackage` and `protectedPackagesBlocked` to `types.h`.
- Dynamic core collection discovery from `/etc/prt-get.conf` (`CruxAdapter::coreCollectionPath()`).
- Protected packages overridden by overlays via disk presence verification (`CruxAdapter::isCorePackage()`).
- Fail-closed behavior: blocks all removals if no `core` collection is configured in `/etc/prt-get.conf`.
- Zero removal steps/argv generated when protected package removal is attempted.
- UI button and context menu item disabled with tooltip in `MainWindow`.
- All 5 required policy unit tests passing (100% test coverage).

### 7.6 Milestone 12 — SCOFS Protocol Resolution: Execution Engine, Batching & Live Streaming (Completed & Verified)
Resolved fatal runner disconnect, password prompt multiplicity, and shell injection:
- Disconnected `m_runner->run` from `executeTransaction` and `syncPortsTree`; wired to asynchronous `CruxProcessRunner`.
- Eliminated legacy 30000ms timeout on mutations.
- Steps of the same action are batched into one command invocation (`depinst pkg1 pkg2 ...`); a plan mixing actions runs one elevated invocation per action group (see §3.12, corrected 2026-10-20).
- Replaced dangerous `SIGKILL` escalation watchdog with graceful `SIGTERM` termination.
- Applied POSIX single-quote shell escaping to `su -c` in `PrivilegeHelper`.
- Validated via real-process integration test (`test_live_transaction_execution`) running for 34 seconds with line-by-line live streaming, prompt cancellation handling, and probe-log verified single-command execution.
- All 11 test suites passing 100% green in 37.38s.

### 7.6.1 Milestone 13 — T480s Session 2026-10-20 (Reviewed, Merged)
Branch `fix/analyzer-and-runner`, one commit per fix, fast-forwarded into `main` and pushed by the owner (`3a7755a`); two review follow-ups were committed directly on `main` and pushed (`796861b` offline fixture generator and stalled-cancel wording, `904aceb` window close refused during an operation):
- `0aeac3e` BuildLogAnalyzer rewritten from the exact pkgutils 5.40.12 pkgmk messages; exit code values are ignored (prt-get's mapping is undocumented; pkgmk exits 1 for footprint/build/install). Fixtures are real pkgmk output from unprivileged sandbox runs (`tools/gen_pkgmk_failure_logs.sh`, `tests/fixtures/crux3.8/pkgmk-failures/`). See OPEN_QUESTIONS.md 2.14.
- `fef1538` CruxProcessRunner closes the child's stdin after start (interactive prompt → EOF, not a hang).
- `c329627` Cancellation watchdog: after 10 s a warning line in the Output tab (plus a `cancellationStalled()` signal no UI code uses yet), Cancel stays usable, never SIGKILL. The QProcess-destructor SIGKILL path is kept out of normal use: `MainWindow` refuses to close during an operation (owner decision).
- `1b3e333` README/ARCHITECTURE no longer claim orphan-free cancellation or process-group signalling.
- `3a7755a` `test_live_transaction_execution` labelled `slow` (`ctest -LE slow`).
- Not done (owner-supervised): GUI checklist (§6), first real mutation (§7), cancellation test 2.10.3 (§8).

### 7.6.2 First Real Operations Through OctoCRUX (2026-10-20, owner-driven)
Details in OPEN_QUESTIONS 2.15:
- Unlock `rust` (intentional, `rust-bin` in use) and Sync worked.
- Upgrade with nothing selected: the preview dialog appeared and the owner pressed Apply. No qt-sudo password prompt appeared (sudo credential cache, hypothesis), so the preview is the only guaranteed gate.
- criu and elfutils failed with footprint mismatches (NEW files only).
- Cancel during the linux-firmware download stopped the whole root tree; nothing was installed; the Output tab showed "Operation was cancelled by user".
- Still open: Cancel during compilation. (The status bar overlap was fixed in `adf9c14`.)
- 2026-10-21 ~01:35: first real Upgrade All (6 ports). elfutils failed the footprint check (NEW only), OctoCRUX offered "Retry with -in", and the retry installed it. Outdated 10 -> 4 (only the locked ones).
- 2026-10-21 07:17: Cancel during a zsh compilation (`configure` -> `gcc` as root) stopped the whole tree within ~1 s, with no orphans (test 2.10.3). The work dir was left, and a SIGPIPE hypothesis is open.
- 2026-10-21: criu upgraded with a `-in` exception (first real exception, verified from the Output); qt-sudo's prompt was suppressed by a later `NOPASSWD` sudoers rule (owner experiment); cancelled and wrong-password authentication paths recorded and turned into a fixture; cancelled authentication now reports `AuthenticationError`; the rejected-config alert lists only new files.

### 7.7 Phase 5 — System Tray Notifier (`octocrux-notifier`) & Settings
*Status 2026-10-21:* Phase 5c steps 1 and 2 are done (ARCHITECTURE §5.5):
- step 1: a read-only tray notifier, manual `ports -u`, and shared settings with the sync time (the owner confirmed the icon and tooltip);
- step 2: automatic sync through pkexec (opt-in polkit rule) or `sudo -n`, never asking for a password unattended, plus the Options dialog.
Step 3 is also done: "Upgrade All..." in the tray opens OctoCRUX on the preview (`--upgrade-all`), and an autostart desktop file is shipped but not enabled. 5a (packaging) is done:
- GNUInstallDirs install rules, `octocrux.desktop`, an SVG icon, and the polkit action installed by the package;
- `OCTOCRUX_INSTALL_DOCS` (off in the port), a single `OCTOCRUX_VERSION` from `project()`, and `--help`/`--version` for both binaries;
- `packaging/crux/octocrux/Pkgfile`, built unprivileged in a sandbox from a local tarball (pkgmk rc 0; the file list matches).
The owner tags `v0.1.0`, copies the port into d77crux, and generates the footprint and signature. Next: 5b (general settings). 5a (packaging: install rules, `.desktop`, d77crux `Pkgfile`) and 5b (general settings dialog) follow. Owner decisions: once a day is the suggested automatic mode, changeable in the GUI; autostart is opt-in per user.

1. **System Tray Daemon (`octocrux-notifier`):**
   - Lightweight standalone background tray indicator (`QSystemTrayIcon`).
   - Periodic, **read-only** check for outdated packages against the ports tree already on disk: `prt-get printf` with `%i == diff` combined with `%l` (OPEN_QUESTIONS 2.5), never `prt-get diff`. The notifier runs unprivileged and must **not** run `ports -u`: that rewrites `/usr/ports` and needs root, so syncing stays an explicit, elevated user action (the Sync button). The check therefore only sees updates after a sync (by the user or a system cron job); how to present that is **[open]** for the Phase 5 design.
   - Passive notification balloons on new updates found.
   - Context menu to launch `octocrux` or trigger immediate update.
2. **Settings & Configuration Storage:**
   - Persistent preferences (`QSettings`) for the preferred privilege helper (`qt-sudo`, `sudo`, `doas`) and check intervals. **No build-directory setting:** `PKGMK_WORK_ROOT` is not a CRUX variable (it exists only in the owner's `/etc/pkgmk.conf`) and `sudo` strips it (`env_reset`), see §3.3 item 1 and ARCHITECTURE §2.2. The work directory comes from `pkgmk.conf`; OctoCRUX only inspects its mount (`/proc/self/mountinfo`, `statvfs`).

