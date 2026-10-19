# OctoCRUX — Open Questions & Verification Plan

**Status:** Updated with CRUX 3.8 Diagnostic Results  
**Review Date:** October 19, 2026  
**Reference Documents:**  
- [RESEARCH_FINDINGS.md](RESEARCH_FINDINGS.md)  
- [BASE_SELECTION.md](BASE_SELECTION.md)  
- [AGENTS.md](AGENTS.md) (SCOFS Protocol)  
- Diagnostic fixtures in `tests/fixtures/crux3.8/` (from `octocrux-diag-20261019-164451.tar.gz`)

---

## 1. Purpose & Verification Status

In accordance with [RESEARCH.md](RESEARCH.md) and the SCOFS protocol in [AGENTS.md](AGENTS.md), this document records technical findings, edge cases, and remaining open questions on target CRUX systems.

### Evidence Labels
- **[verified]** Tested and reproduced on a CRUX 3.8 system, with captured outputs kept as fixtures in `tests/fixtures/crux3.8/`.
- **[verified on test machine]** Verified on the tested hardware/environment; not assumed universally identical across all machines.
- **[owner-reported]** Observed by the project owner on real CRUX systems; awaiting captured fixture or disposable-environment test.
- **[hypothesis]** Untested hypothesis from documentation or analysis.
- **[open]** Active open question requiring architectural decision or further test.

---

## 2. Findings and Status by Topic

### 2.1 `prt-get printf` Formatting and Delimiters
* **Status: [verified]** (Fixture: `tests/fixtures/crux3.8/cmd/printf-literaltab.out`)
* **Verified Behavior:**
  1. `prt-get printf` interprets `\t` and `\n` cleanly without appending redundant newlines.
  2. In the full tree scan of 2,307 ports, **zero lines** had field count mismatches (all lines strictly 8 tab-separated tokens).
  3. `%i` values are strictly `"yes"`, `"no"`, or `"diff"`.
  4. `%e` (dependencies) is comma-separated without spaces (e.g. `dep1,dep2`), and is empty for 450 ports.
  5. `%P` (Packager) is empty across all 2,307 ports; it cannot be relied upon for UI metadata.
  6. Empty fields produce adjacent tab delimiters (`\t\t`) predictably.

### 2.2 Cache Output Pollution & Execution Speed
* **Status: [verified on test machine]** (Fixture: `tests/fixtures/crux3.8/cmd/printf-nocache.ms`)
* **Verified Behavior:**
  1. Full ports tree query (2,307 ports) across enabled collections took only **69 ms** without cache on a ThinkPad T480s (SSD).
  2. The binary cache file `/var/lib/pkg/prt-get.cache` was not present on the test system, causing `--cache` and `prt-cache` to exit with rc=255 (`"Can't open cache file"`).
  3. *Hardware caveat:* While 69 ms makes cache unnecessary on modern hardware, caching may still benefit slower CPUs or mechanical disks. It is recorded as verified on this hardware, not assumed globally obsolete.

### 2.3 Multi-Collection Overlay Precedence & Duplicate Ports
* **Status: [verified]** (Fixtures: `duplicate-names-from-printf.txt`, `prtget-dup.out`)
* **Verified Behavior:**
  1. `prt-get printf` returns exactly one entry per unique port name, with repository precedence already resolved according to `/etc/prt-get.conf`'s `prtdir` order (e.g., `gdk-pixbuf` version 2-2 selected from `/usr/ports/d77crux`).
  2. Total duplicate port names in printf output: **0**.
  3. Duplicate conflicts across collections are inspected via `prt-get dup`, which prints in `wins > loses` format with version tags.

### 2.4 Locked Packages & Three-Axis Data Model
* **Status: [verified]** (Fixtures: `printf-token-l.out`, `prtget-listlocked.out`)
* **Verified Behavior:**
  1. Package locks are stored in `/var/lib/pkg/prt-get.locker` (one port name per line).
  2. Token `%l` works reliably and returns `"yes"` or `"no"`.
  3. The test system contained 5 locked packages: `llvm`, `clang`, `lld`, `compiler-rt`, `rust`.
* **Three-Axis Orthogonal Data Model:**
  Port status must not be modeled as a flat enumeration. It consists of three independent, orthogonal axes:
  - **Axis 1 (Installation):** `%i` -> `"yes"`, `"no"`, `"diff"`.
  - **Axis 2 (Lock Status):** `%l` -> `"yes"`, `"no"`.
  - **Axis 3 (Alias / Provided By):** Mapped from `/etc/prt-get.aliases` (e.g. `rust` provided by `rust-bin`).
  
  The 6 valid combinations of `%i` × `%l`:
  | `%i` | `%l` | OctoCRUX Classification | Meaning |
  | :--- | :--- | :--- | :--- |
  | `yes` | `no` | Installed | Package installed and matching ports tree. |
  | `yes` | `yes` | Installed & Locked | Package installed, up to date, explicitly locked against changes. |
  | `diff` | `no` | Outdated | Package installed, tree has newer version, eligible for upgrade. |
  | `diff` | `yes` | Outdated & Locked | Package installed, tree has newer version, but locked. |
  | `no` | `no` | Available | Not installed. |
  | `no` | `yes` | Locked Uninstalled | Uninstalled port explicitly locked (e.g. `rust` locked in `opt` to prevent accidental source compilation when `rust-bin` is installed). |

### 2.5 Outdated Detection & Version Parsing (`prt-get diff` Blindness)
* **Status: [verified]** (Fixtures: `prtget-diff.out`, `prtget-quickdiff.out`, `diff-from-*.txt`)
* **Verified Behavior:**
  1. **Critical Defect in `diff` / `quickdiff`:** Both commands **silently suppress locked packages**! The 4 discrepancies between `%i=diff` and `quickdiff` are precisely `llvm`, `clang`, `lld`, and `compiler-rt`. `prt-get diff` reports `"No differences found"` (rc=0) when only locked packages are outdated.
  2. `prt-get diff` prints human-oriented aligned columns, not suitable for machine parsing.
  3. **Architectural Rule:** OctoCRUX must use `prt-get printf` (`%i == diff` combined with `%l`) as the authoritative source for available updates.

### 2.6 Reverse Dependencies (`dependent`) Semantics
* **Status: [verified with `gtk3` sample]**
* **Verified Behavior:**
  1. `prt-get dependent <port>` (without `--all`): Lists **only installed** direct dependents (7 packages for `gtk3`). This is the authoritative command for **removal impact analysis**.
  2. `prt-get dependent --all <port>`: Lists **all** ports in the tree declaring `<port>` as a dependency (38 packages for `gtk3`, of which 31 are uninstalled). Used for navigation/browsing, NOT removal impact.
  3. Both variants report direct dependents only (non-recursive).
  4. **Exit Code Trap:** `prt-get dependent <nonexistent-port>` exits with rc=0 and empty stdout (identical to an existing port with no dependents). OctoCRUX backend must validate that a port exists before invoking `dependent`.

### 2.7 Installation Order & Dependency Resolution (`quickdep` vs `depends`)
* **Status: [verified with `gtk3` sample]**
* **Verified Behavior:**
  1. `prt-get quickdep <port>` returns the **complete recursive installation order**, ending with the target port itself, with leading/trailing spaces (e.g. `" boehm-gc paper a2ps  "`).
  2. `quickdep` collapses aliases to their target port name.
  3. `prt-get depends <port>` outputs the same list with human-readable headers, `[i]` installation tags, and alias notes (e.g. `rust (provided by rust-bin)`).
  4. **Parser Requirement:** The backend parser must strip whitespace and split by space for `quickdep`, and use `/etc/prt-get.aliases` to re-associate virtual providers.

### 2.8 Footprint Mismatches, Integrity & Recovery (`-in`, `-if`, `.zst`)
* **Status: [verified / hypothesis]**
* **Verified Behavior:**
  1. Built packages follow the format `/usr/ports/packages/<name>#<version>-<release>.pkg.tar.zst` (**Zstandard** compression verified).
  2. Footprint checking reports only `NEW` and `MISSING` entries. Permission or ownership changes appear as a `MISSING` + `NEW` pair.
  3. `pkgmk` advertises **`-in`** (ignore ONLY `NEW` files), which is narrower and safer than `-if` (ignore all footprint discrepancies).
  4. `/var/lib/pkg/rejected`: `pkgadd` stores rejected configurations here during upgrades, awaiting `rejmerge`. OctoCRUX should check for entries post-transaction.
  5. Per-port build logs: With `writelog enabled` and `rmlog_on_success yes` in `/etc/prt-get.conf`, `/var/log/pkgbuild/%n.log` stores logs only for failed ports during `sysup`.
  6. Direct `pkgadd -u` recovery caveat: `pkgadd` does not execute `pre-install` or `post-install` scripts.

### 2.9 Subprocess Execution, Terminal & Privilege Escalation
* **Status: [verified on test machine / open]**
* **Verified Behavior on Test Environment:**
  - `qt-sudo 2.4.1` and `sudo 1.9.17p2` installed. `doas` absent.
  - `qtermwidget6` is **not installed** in the test system.
  - Qt 6.12.0 and CMake 3.31.12 confirmed.
* **Confirmed Architectural Decisions:**
  1. **Headless `QProcess` is Mandatory:** Because `qtermwidget6` is not present in standard CRUX setups, OctoCRUX must implement a first-class headless `QProcess` log streaming path, with optional fallback to user-configured external terminal emulators.
  2. **Decided Privilege Direction:** The GUI application always runs unprivileged. Elevation is invoked per-transaction for mutating operations (`ports -u`, `depinst`, `update`, `remove`, `lock`).
* **Remaining Open Questions:**
  1. **[open] Interactive Prompts in Builds:** How to handle rare interactive prompts in `Pkgfile` scripts under a headless `QProcess`? (Requires external terminal fallback or lightweight PTY helper).
  2. **[open] Cross-UID Cancellation:** An unprivileged GUI cannot send `SIGINT`/`SIGTERM` to a root process group (`EPERM`). PTY line discipline (`^C`) or elevation helper signal relaying must be verified in disposable environment.

### 2.10 `pkginfo -o` Pattern Semantics & POSIX ERE Escaping
* **Status: [verified on test machine]** (Fixtures: `pkginfo-o-disc-regex-dot.out`, `pkginfo-o-disc-glob-qmark.out`, `pkginfo-o-disc-regex-escaped.out`, `pkginfo-o-stdcxx-escaped.out`, from `octocrux-prod-capture-20261019-203243.tar.gz`)
* **Verified Behavior on CRUX 3.8 / pkgutils 5.40.12:**
  1. `pkginfo -o <pattern>` evaluates patterns using **POSIX Extended Regular Expressions (ERE)** via `regcomp`/`regexec`, NOT shell globbing (`fnmatch`).
     - Discriminator proof: `pkginfo -o usr/bin/prt.get` matches `prt-get` (`.` matches any char); `pkginfo -o "usr/bin/prt?get"` matches nothing (`?` fails to match `-`).
  2. Substring matching: `pkginfo -o` performs unanchored substring searches. A pattern like `usr/lib/libstdc\+\+\.so\.6` returns multiple candidate files (`.so.6`, `.so.6.0.33`, `-gdb.py`).
  3. Metacharacter traps: Unescaped `+` in `/usr/bin/g++` acts as an ERE quantifier, matching 192 files under `usr/bin/g*`. Unescaped `/usr/lib/libstdc++.so.6` fails completely ("no owner found").
  4. Anchors (`^`, `$`) fail on relative paths (e.g. `^usr/bin/g\+\+$` finds no owner).
* **Architectural Rules ([`CruxAdapter::findPackageOwnerOfFile`](src/backend/crux_adapter.cpp)):**
  1. Normalize path by stripping leading slashes (`/`).
  2. Escape all POSIX ERE metacharacters (`. [ \ ( ) * + ? { | ^ $`) via `CruxAdapter::escapeErePattern`. Do NOT escape `/` or `-` (which are literal in POSIX ERE and broken by PCRE escape functions).
  3. Invoke `pkginfo -o <escapedPattern>`. Do NOT add `^` or `$`.
  4. Post-filter all returned records requiring exact equality: `candidateFile == normalizedTarget`.

### 2.11 Transaction Planning, Dependency Partitioning & Safety Guards
* **Status: [verified]** (Fixtures: `inst-quickdep.out`, `inst-dependent.out`, `prt-get.aliases`)
* **Verified Behavior:**
  1. `prt-get quickdep` outputs the full recursive dependency chain (e.g. 118 packages for `gtk3`). A naive planner would queue 118 builds. `TransactionPlanner` partitions the DAG into uninstalled packages (`toInstall`), outdated packages (`toUpdate`), and already-installed up-to-date packages (`satisfiedDependencies`, >50 packages for `gtk3`).
  2. Locked package suppression in `sysup`: `prt-get diff` silently hides locked packages (`llvm`, `clang`, `lld`, `compiler-rt`). `planUpgrade` catches all 4 outdated locked packages and puts them in `lockedSkipped` with explanatory warnings.
  3. Locked uninstalled ports (e.g. `rust` in `opt` to prevent 6-hour builds when `rust-bin` is used) are blocked from build attempts unless satisfied by an installed alias provider.
  4. Multi-tier recursive reverse dependents cascade: Standard `dependent` only checks direct 1-level dependents. `TransactionPlanner::calculateRecursiveDependentsCascade` executes a BFS cascade to compute the complete chain of broken installed packages.
  5. Core CRUX packages (`glibc`, `pkgutils`, `prt-get`, `coreutils`, etc.) trigger critical system warnings upon removal.

### 2.12 Linux `/proc` Pseudo-filesystem Zero-Size Quirk & Mount Inspection
* **Status: [verified]**
* **Verified Behavior:**
  1. Reading `/proc/self/mountinfo` via `QTextStream::atEnd()` or `QFile::atEnd()` fails because kernel `stat(2)` reports `st_size = 0` for `/proc` pseudo-files, causing `atEnd()` to return true before any lines are read.
  2. Direct byte reading with `file.readAll()` completely bypasses the zero-size quirk and reliably provides the mount point and filesystem type (`tmpfs` vs disk) across Linux kernels.
  3. Available disk headroom is obtained factually via `statvfs(2)` (`f_bavail * f_frsize`).

---

## 3. Exit Codes & Command Quirks Reference for Parsers

| Command | Expected rc on Success | Behavior on Missing / Error | Output Quirk for Parsers |
| :--- | :--- | :--- | :--- |
| `prt-get printf` | `0` | Returns `0` even if some tokens empty | Empty tokens produce `\t\t`; canonical 11 tokens (`%d` last) isolate tabs. |
| `prt-get dependent <port>` | `0` | Nonexistent port returns `0` and empty stdout | Must verify port existence before calling. |
| `prt-get isinst <port>` | `0` (installed) | Returns `255` for BOTH uninstalled and error | Do NOT use for status; use `printf %i` or `pkginfo -i`. |
| `prt-get diff` | `0` | Returns `"No differences found"` with rc=0 | Suppresses locked packages; do not use for automated diff parsing. |
| `prt-get quickdep <port>` | `0` | Returns `255` on nonexistent port | Recursive order, ends with port itself, leading/trailing whitespace. |
| `pkginfo -o <pattern>` | `0` | Returns `1` if file not owned | Header line `Package  File`; unanchored POSIX ERE regex; requires `escapeErePattern` and exact post-filtering. |

---

## 4. SCOFS Validation Ladder & Test Fixtures

All Level 1 & Level 2 fixtures have been extracted into `tests/fixtures/crux3.8/cmd/`:
- `printf-production-11token.cmd` / `.out`: Canonical 11-token production fixture of 2,307 ports with full execution provenance.
- `printf-literaltab.out`: 2,307 ports formatted with tab delimiters (diagnostic Run 1).
- `prtget-listlocked.out`: Real list of locked packages (`llvm`, `clang`, `lld`, `compiler-rt`, `rust`).
- `prtget-dup.out`: Duplicate collection conflicts.
- `pkginfo-o-disc-regex-escaped.cmd` / `.out`: Verified ERE regex-escaped pattern query (`usr/bin/g\+\+`).
- `pkginfo-o-stdcxx-escaped.cmd` / `.out`: Verified substring candidate post-filtering (`usr/lib/libstdc\+\+\.so\.6`).
- `pkginfo-i.out`: Real installed package inventory.
- Man pages: `man-prt-get.out`, `man-pkgmk.out`, `man-pkgadd.out`, etc.

These fixtures are used directly in `QTest` unit test suites for the parser and backend adapter.
