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
  5. Core CRUX package removal protection (superseded by Section 2.13): Previously used a hardcoded list; now strictly governed by the disk-checked Core Package Protection Policy.

### 2.12 Linux `/proc` Pseudo-filesystem Zero-Size Quirk & Mount Inspection
* **Status: [verified]**
* **Verified Behavior:**
  1. Reading `/proc/self/mountinfo` via `QTextStream::atEnd()` or `QFile::atEnd()` fails because kernel `stat(2)` reports `st_size = 0` for `/proc` pseudo-files, causing `atEnd()` to return true before any lines are read.
  2. Direct byte reading with `file.readAll()` completely bypasses the zero-size quirk and reliably provides the mount point and filesystem type (`tmpfs` vs disk) across Linux kernels.
  3. Available disk headroom is obtained factually via `statvfs(2)` (`f_bavail * f_frsize`).

### 2.13 Core Package Protection Policy & Fail-Closed Safety
* **Status: [verified]** (Fixtures: `printf-production-11token.out`, `tests/fixtures/crux3.8/cmd/`)
* **Reference Document:** [`OctoCRUX — Core package protection policy.md`](OctoCRUX%20%E2%80%94%20Core%20package%20protection%20policy.md) (Daniel Azevedo, 2026-10-19)
* **Empirical Facts on CRUX 3.8 Bare Metal:**
  1. 126 ports in `core` collection. 126 of 126 installed on sysvd77.
  2. In the entire 2,307-port tree, only 1 port (`libxcrypt`) declares `glibc` in `%e`.
  3. 68 of 126 core ports have empty `%e`. Core ports assume the core foundation is present.
  4. Any dependency-count heuristic would classify `glibc` as virtually unused and safe to remove.
* **Authoritative Protection Invariants Implemented:**
  1. **Strict GUI Removal Block:** OctoCRUX never removes a package belonging to the CRUX core collection. No typed-confirmation or override inside the GUI. Escape hatch is external only (`prt-get remove` or `pkgrm` in a terminal).
  2. **Dynamic Collection Discovery (*Fail-Closed*):** Discover the `core` collection directory by parsing `prtdir` directives in `/etc/prt-get.conf` and selecting the collection whose name/basename is `core`. If no `core` collection is configured, fail closed: block all removals and report why.
  3. **Check Membership on Disk:** A core package overridden by an overlay (e.g. `d77crux/glibc`) reports the overlay's `%p` and `%R`, and would silently lose protection if checked via winning port collection. Verification must inspect whether directory `<core collection>/<name>` exists on disk.
  4. **Single Source of Truth:** Brittle hardcoded lists (`criticalSystemPackages()`) are completely eliminated.
  5. **Outcome:** Blocked removals return `OperationErrorKind::ProtectedPackage`, populate `protectedPackagesBlocked`, add zero removal steps/argv to the plan, and emit the policy message.
  6. **UI Disabled Actions:** `MainWindow` disables the Remove button and context menu item with the tooltip `"Core packages cannot be removed in OctoCRUX"`.

### 2.14 `pkgmk` Exit Codes, Messages, Build Logs & `prt-get --test` (Execution Capture)
* **Status: [verified on test machine]** (Capture: `octocrux-exec-capture-20261020-214106.tar.gz` from `tools/crux_capture_execution.sh`, sysvd77 / T480s, 2026-10-20; failure fixtures: `tests/fixtures/crux3.8/pkgmk-failures/` from `tools/gen_pkgmk_failure_logs.sh`)
* **`pkgmk` (pkgutils 5.40.12) exit codes as shipped:**
  1. Defined: `E_GENERAL=1`, `E_PKGFILE=2`, `E_DIR_PERM=3`, `E_DOWNLOAD=4`, `E_UNPACK=5`, `E_MD5=6`, `E_FOOTPRINT=7`, `E_BUILD=8`, `E_INSTALL=9`, **`E_SIGNATURE=10`** (not in the man page's 0–9 list).
  2. **`E_FOOTPRINT`, `E_BUILD` and `E_INSTALL` are defined but never used.** Footprint mismatch, `build()` failure and `pkgadd` failure all `exit 1`, as does `interrupted()`. Reproduced: `footprint.rc`, `build-fail.rc`, `interrupt.rc` are all `1`.
  3. Codes actually emitted: 0, 1, 2, 3, 4, 5, 6, 10.
  4. `prt-get(8)` RETURN VALUE: "0 on success and a non-zero value otherwise (exact value -> meaning mapping to be determined...)". Whether prt-get forwards pkgmk's code is **[open]** (no prt-get failure exit code captured yet).
  5. **Rule:** `BuildLogAnalyzer` never maps an exit code value to an error kind; it only uses `0` vs non-zero, and classifies from the exact pkgmk message lines below.
* **Message format:** `info()` prints `=======> <msg>` (7 `=`, one space) on stdout; `warning()` / `error()` print `=======> WARNING: <msg>` / `=======> ERROR: <msg>` on **stderr**. The `build()` xtrace (`+ build`, from `(set -e -x ; build)`) is also on stderr. A failing step is always followed by `=======> ERROR: Building '<target>' failed.` except for Pkgfile/directory/download errors, which exit directly.
* **Exact error messages used for classification** (variable part in `<>`):
  | Kind | pkgmk 5.40.12 message |
  | :--- | :--- |
  | SignatureError | `Signature mismatch found:` · `Failed verifying cryptographic digest` · `Creating signature failed` · `Refreshing signature failed` |
  | ChecksumError | `Md5sum mismatch found:` · `Md5sum not ok.` |
  | FootprintError | `Footprint mismatch found:` |
  | DownloadError | `Downloading '<url>' failed.` · `Source file '<f>' not found (use option -d to download).` · `Source file '<f>' not found (can not be downloaded, URL not specified).` · `Command '<prog>' not found.` |
  | PkgfileError | `Variable '<v>' not specified in <Pkgfile>.` · `Function 'build' not specified in <Pkgfile>.` · `File '<f>' not found.` |
  | SourceDirError | `Directory '<d>' does not exist.` / `not writable.` / `not readable.` · `File '<f>' is not writable.` |
  | InstallError | `Installing '<pkg>' failed.` (pkgmk `-i`/`-u` only; prt-get runs pkgadd itself) · prt-get fragment `error while pkgadding` **[hypothesis: from binary strings, full line not observed]** |
  | UnpackError | `Building '<pkg>' failed.` with no `+ build` trace in that port |
  | CompilationError | `Building '<pkg>' failed.` after `+ build` |
  | DiskSpaceError | `No space left on device` (libc strerror, any tool) |
  The former patterns `"Permission denied"` (matched build() commands, see `build-eacces.log`), `"pkgadd:"`, `"ENOSPC"`, `"curl: ("`, `"tar: Error"`, `": error:"`, `"make: ***"` and the invented `pkgmk: ...` strings were removed.
* **Failure fixtures:** real output of the installed pkgmk, run **unprivileged** with `-cf <sandbox conf>` (so `/etc/pkgmk.conf` and its signing key are never read), against synthetic sandbox ports, one per failure class. No pkgadd, no network (download uses a missing `file://` path). Install failures through prt-get cannot be reproduced this way **[open: needs a real failing install, see session plan §7]**.
* **Signals:** pkgmk traps `SIGHUP SIGINT SIGQUIT SIGTERM` and prints `=======> ERROR: Interrupted.`, then `exit 1`. **[verified]** A pkgmk started as a background job of a non-interactive shell inherits SIGINT as ignored, and bash cannot trap a signal ignored at entry: `kill -INT` was silently ignored (build ran to completion); `kill -TERM` worked.
* **`/var/log/pkgbuild` contents (26 logs):** contrary to the expectation that leftovers are failures, most are **successful** builds from 2026-09-16 (contain `Building '...' succeeded.`) **[verified]**; why they survived `rmlog_on_success yes` is **[open]**. `qemu.log` and `gcc-fortran.log` end mid-compilation. Two logs from 2026-10-20 (`linux-firmware.log` at 21:36:30, `elfutils.log` at 21:38:15) were left by an owner-run `sysup` stopped with **Ctrl-C** **[owner-reported]**; neither contains `ERROR: Interrupted.`, and prt-get went on to the next port after the first interruption **[verified from the logs' `starting build` lines]**. Why the line is missing is **[hypothesis: prt-get, which writes the log, died before pkgmk's trap output reached it]**. Logs are not committed until reviewed by the owner.
* **`prt-get --test` (dry run):**
  | Command | rc | Output |
  | :--- | :--- | :--- |
  | `prt-get --test update llvm` (locked, outdated) | `0` | `-- Packages updated` / `llvm` / `prt-get: updated successfully`: the dry run **does not refuse a locked port named explicitly** **[verified]**; whether a real `update llvm` also ignores the lock is **[open]** |
  | `prt-get --test sysup` | `0` | 7 ports (`elfutils linux-firmware python3-setuptools xfsprogs volume_key criu pcsclite`); the 4 locked outdated ports are excluded (11 outdated in total) |
  | `prt-get --test remove glibc` | `0` | `-- Packages removed` / `glibc`: **prt-get has no core protection of its own**; OctoCRUX's policy (2.13) is the only guard |
* **Capture script defect:** `crux_capture_execution.sh` greps `E_[A-Z_]+=`, which misses `E_MD5=6` (digit in the name). The table above was read from the copied `pkgmk` itself.

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
