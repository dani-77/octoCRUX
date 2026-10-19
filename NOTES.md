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
- **Tool Created:** [`crux_diagnostics.sh`](crux_diagnostics.sh) (strictly read-only POSIX diagnostic script).
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
  6. *Critical System Package Safeguards:* Removal of core CRUX packages (`glibc`, `pkgutils`, `prt-get`, `coreutils`, `bash`, `binutils`, `gcc`, etc.) triggers high-priority `criticalWarnings`.
- **Tested in [`tests/test_transaction_planner.cpp`](tests/test_transaction_planner.cpp):** 12 comprehensive unit tests covering all edge cases, mount inspection, and SCOFS scenarios (100% green in 38 ms).

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

---

## 5. Current Test Verification Evidence

All tests executed via `ctest` in the project build environment:

```text
Test project /home/dani77/Projectos/octoCRUX/build
    Start 1: test_printf_parser
1/7 Test #1: test_printf_parser ...............   Passed    0.14 sec
    Start 2: test_aliases_parser
2/7 Test #2: test_aliases_parser ..............   Passed    0.07 sec
    Start 3: test_dependency_parser
3/7 Test #3: test_dependency_parser ...........   Passed    0.07 sec
    Start 4: test_crux_adapter
4/7 Test #4: test_crux_adapter ................   Passed    0.11 sec
    Start 5: test_package_model
5/7 Test #5: test_package_model ...............   Passed    0.13 sec
    Start 6: test_mainwindow
6/7 Test #6: test_mainwindow ..................   Passed    0.16 sec
    Start 7: test_transaction_planner
7/7 Test #7: test_transaction_planner .........   Passed    0.11 sec

100% tests passed, 0 tests failed out of 7
Total Test time (real) = 0.82 sec
```

### Breakdown of Executed Test Suites (64 individual test functions):
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
4. **`test_crux_adapter` (11 test functions passed):**
   - `testQueryAllPortsAndThreeAxes`: End-to-end catalog query using real `printf-production-11token.out` fixture with provenance check.
   - `testQueryPort`: Existing port lookup (`gtk3`) and nonexistent port handling.
   - `testQueryOutdatedAndLockedPorts`: Set checks for outdated and locked ports.
   - `testQueryAliases`: Virtual to provider mapping.
   - `testDependents`: Direct (7) and all (38) dependents, guarded against CRUX nonexistent port quirk.
   - `testQuickdepWithAliasesResolution`: Full recursive dependency order with alias substitution.
   - `testReadPortFile`: Real file reading of `Pkgfile` and `.footprint` from fixture root.
   - `testPkginfoFilesAndOwner`: `pkginfo -l` file listing; `pkginfo -o` owner extraction verifying POSIX ERE regex escaping (`escapeErePattern`) on `/usr/bin/g++` and exact-match post-filtering across multiple substring candidates on `/usr/lib/libstdc++.so.6`.
   - `testMutationStubs`: Confirms mutation stubs safely return false in Phase 1.
5. **`test_package_model` (11 test functions passed):**
   - `testModelStructure`: 2,307 rows, 5 columns, headers, O(1) port lookup.
   - `testDisplayDataAndThreeAxesFormatting`: Formatting of Installed, Outdated (Locked), Virtual [Locked], Available.
   - `testFilterAll` / `testFilterOutdated` / `testFilterLocked` / `testFilterInstalledAndVirtual`: Multi-dimensional filtering across domain axes.
   - `testCollectionFilter`: Overlay filtering (`d77crux`).
   - `testSearchFilter`: Substring search in name and description.
   - `testSorting`: In-memory sorting ascending/descending.
6. **`test_mainwindow` (7 test functions passed):**
   - `initTestCase`: Initializes headless `MainWindow` loaded with mock `CruxAdapter` containing production fixture data.
   - `testInitialPopulationAndCounts`: Verifies status bar labels and proxy model row count matches 2,307 ports.
   - `testStatusFilterButtons`: Verifies exclusive status button switching (*Installed*, *Outdated*, *Locked*, *Available*, *All*) and corresponding filtered row counts.
   - `testCollectionFilterCombo`: Verifies collection combo switching (e.g. `core`, `opt`, `d77crux`) filters the table view accurately.
   - `testSearchBoxFiltering`: Verifies real-time text input filtering on package names and descriptions.
   - `testPortSelectionUpdatesDetailTabs`: Selects a port row in `QTableView` and validates that the 5 detail tabs (*Information*, *Files*, *Pkgfile*, *Footprint*, *Readme*) update with the port's metadata and recipe files.
   - `cleanupTestCase`: Cleans up GUI resources cleanly.
7. **`test_transaction_planner` (12 test functions passed):**
   - `initTestCase`: Initializes `CruxAdapter` with production fixtures and `inst-quickdep.out`/`inst-dependent.out`.
   - `testInstallPlanSimplePort`: Tests `a2ps` uninstalled target and verifies topological build ordering (`boehm-gc` -> `paper` -> `a2ps`); validates `acl` where both `acl` and `attr` are already satisfied.
   - `testInstallPlanDependenciesPartitioning`: Validates `gtk3` install plan: verifies that the 118 packages in quickdep are partitioned into actions vs satisfied dependencies (>50 already satisfied), preventing redundant build queues.
   - `testInstallPlanLockedUninstalledError`: Verifies that attempting to compile an explicitly locked uninstalled port (`rust` in `opt`) produces an invalid plan with an explanatory error.
   - `testUpgradePlanAllOutdatedWithLocks`: Verifies that `planUpgrade()` detects all 4 outdated locked packages (`llvm`, `clang`, `lld`, `compiler-rt`), does not queue them in `toUpdate`, and places them in `lockedSkipped` with warnings.
   - `testRemovalPlanDirectDependents`: Verifies that removing `gtk3` detects exactly the 7 installed direct dependents (`tint2`, `granite`, `firefox-bin`, `lxappearance`, `thunderbird-bin`, `glade`, `libnotify`).
   - `testRemovalPlanRecursiveCascade`: Verifies that multi-level reverse dependents are discovered recursively via BFS cascade (`libnotify` -> `notification-daemon`).
   - `testCriticalSystemPackageSafeguard`: Verifies that removal of essential packages (`glibc`, `pkgutils`, `prt-get`) triggers `criticalWarnings`, while non-critical packages (`a2ps`) do not.
   - `testNonexistentPortHandling`: Verifies that nonexistent ports return invalid plans with informative error messages.
   - `testFilesystemMountInspection`: Verifies empirical `statvfs` byte calculation and `/proc/self/mountinfo` parsing on `/tmp` and `/`.
   - `testLockTogglePlan`: Verifies discrete step creation for locking and unlocking a port.
   - `cleanupTestCase`: Cleans up resources.

---

## 6. Current File Tree

```
octoCRUX/
├── AGENTS.md                                         # Project working agreement & protocols
├── ARCHITECTURE.md                                  # Architectural specification & domain model
├── BASE_SELECTION.md                                # Analysis & selection of Hybrid Approach B
├── CMakeLists.txt                                   # Top-level CMake configuration (C++17, Qt 6 Core & Widgets)
├── crux_capture_production.sh                       # Provenance-preserving production capture script
├── crux_diagnostics.sh                              # Read-only POSIX CRUX 3.8 diagnostic script
├── NOTES.md                                         # This document (temporal log & handoff)
├── OPEN_QUESTIONS.md                                # Unresolved questions & SCOFS tracking ladder
├── README.md                                        # Project overview
├── RESEARCH_FINDINGS.md                             # Upstream audit (Octopi, OctoPkg, OctoXBPS, YAPO)
├── include/
│   └── octocrux/
│       ├── types.h                                  # Domain models (PortInfo, InstallStatus, LockStatus, TransactionPlan, Steps)
│       └── icruxbackend.h                           # Pure abstract backend interface (ICruxBackend)
├── src/
│   ├── backend/
│   │   ├── command_runner.h / .cpp                  # ICommandRunner abstraction & DefaultCommandRunner
│   │   └── crux_adapter.h / .cpp                    # CruxAdapter implementing ICruxBackend
│   ├── core/
│   │   ├── printf_parser.h / .cpp                   # Level 1 prt-get printf parser (11-token canonical format)
│   │   ├── aliases_parser.h / .cpp                  # Level 1 /etc/prt-get.aliases parser
│   │   └── dependency_parser.h / .cpp               # Level 1 dependent & quickdep parser
│   ├── model/
│   │   ├── package_model.h / .cpp                   # QAbstractTableModel for ports catalog
│   │   └── package_filter_proxy_model.h / .cpp      # QSortFilterProxyModel for 3-axis filtering & search
│   ├── operations/
│   │   ├── transaction_planner.h / .cpp             # TransactionPlanner (dependency partitioning, locks, safeguards, mounts)
│   ├── ui/
│   │   ├── mainwindow.h / .cpp                      # Main Qt 6 window (toolbar, table, detail tabs, status bar)
│   └── main.cpp                                     # Application entry point (octocrux executable)
├── tests/
│   ├── CMakeLists.txt                               # Test targets configuration (Qt6::Test)
│   ├── test_printf_parser.cpp                       # Unit tests for PrintfParser (11 tests)
│   ├── test_aliases_parser.cpp                      # Unit tests for AliasesParser (5 tests)
│   ├── test_dependency_parser.cpp                   # Unit tests for DependencyParser (7 tests)
│   ├── test_crux_adapter.cpp                        # Integration tests for CruxAdapter (11 tests)
│   ├── test_package_model.cpp                       # Unit tests for PackageModel & Proxy (11 tests)
│   ├── test_mainwindow.cpp                          # Unit tests for MainWindow GUI (7 tests)
│   ├── test_transaction_planner.cpp                 # Unit tests for TransactionPlanner (12 tests)
│   └── fixtures/
│       ├── crux3.8/                                 # Run 1 diagnostic captures (540 files)
│       │   └── cmd/                                 # Real production fixtures with provenance (.cmd, .out, .err, .rc)
│       └── crux3.8_run2/                            # Run 2 diagnostic captures (gtk3 sample)
└── tools/
    └── octocrux_live_check.cpp                      # Standalone CLI tool for bare-metal CRUX validation
```

---

## 7. Next Steps Roadmap

### 7.1 Level 2 Bare-Metal Validation on T480s (Completed & Verified)
Level 2 validation executed on the bare-metal ThinkPad T480s (CRUX 3.8, pkgutils 5.40.12, prt-get 5.19.9):
- **Canonical 11-Token Production Fixture:** Successfully captured with full execution provenance (`printf-production-11token.cmd`/`.out`). All 2,307 ports validated with zero line errors; `argv` provenance matched `PrintfParser::PRODUCTION_FORMAT` verbatim.
- **`pkginfo -o` POSIX ERE Regex Mechanism:** Empirically proven and verified. `CruxAdapter::escapeErePattern` implemented and verified.
- **Fixture Integration & Unit Tests:** Captured fixtures imported into `tests/fixtures/crux3.8/cmd/`; all 45 test functions passing.

### 7.2 Phase 2 Step 4.2 — Main Window UI (`MainWindow` & `main.cpp`) (Completed & Verified)
Constructed clean Qt 6 graphical interface adhering to Octopi desktop patterns and CRUX principles:
- Implemented `src/ui/mainwindow.h` / `src/ui/mainwindow.cpp`.
- Toolbar with search bar, collection combo, status filter buttons: *All*, *Installed*, *Outdated*, *Locked*, *Available*.
- Central `QTableView` hooked up to `PackageFilterProxyModel`.
- Bottom dock `QTabWidget` displaying real-time details (*Information*, *Files*, *Pkgfile*, *Footprint*, *Readme*).
- Application entry point `src/main.cpp` generating native `octocrux` binary.
- Unit test `tests/test_mainwindow.cpp` validating all components offscreen (7 tests passing).

### 7.3 Phase 3 — Transaction Planner & Operations Layer (Completed & Verified)
Operations planning engine enforcing CRUX invariants and SCOFS safeguards:
- Implemented `include/octocrux/types.h` and `src/operations/transaction_planner.h / .cpp`.
- Dependency partitioning: separates uninstalled builds, updates, and satisfied dependencies.
- Virtual alias mapping (`rust` -> `rust-bin`).
- Outdated locked packages detection (`llvm`, `clang`, `lld`, `compiler-rt`) into `lockedSkipped`.
- Locked uninstalled ports safeguard (`rust` in `opt`).
- Multi-tier reverse dependents cascade and essential system packages (`glibc`, `pkgutils`, `prt-get`) protection.
- Empirical filesystem inspection via `statvfs` and raw byte reading of `/proc/self/mountinfo`.
- Unit test `tests/test_transaction_planner.cpp` (12 tests passing).

### 7.4 Immediate Next Step: Phase 4 — Execution Engine & Privilege Helper
Now that transactions can be computed safely, the execution engine connects the plan to live mutation commands:
1. **Headless Execution Engine (`CruxProcessRunner`):**
   - Non-blocking `QProcess` runner with asynchronous streaming of stdout and stderr into the UI.
   - Real-time line-by-line parsing of `prt-get` and `pkgmk` progress.
   - Classification of errors into `OperationErrorKind` (mirroring `pkgmk(8)` codes 1–9) by reading `/var/log/pkgbuild/<port>.log` upon failure.
2. **Dynamic Privilege Escalation Helper (`PrivilegeHelper`):**
   - Probe available privilege escalation helpers on CRUX (`qt-sudo`, `sudo`, `doas`, `su`).
   - Run GUI completely unprivileged (UID 1000) and invoke helper only for mutating actions (`prt-get depinst`, `update`, `remove`, `lock`, `unlock`, `ports -u`).
3. **SCOFS Test 2.10.3 (Cancellation Verification):**
   - Execute cancellation test in an isolated disposable environment to determine whether `qt-sudo` relays signals to child process groups or requires PTY line discipline (`^C`).
