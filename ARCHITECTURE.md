# OctoCRUX — Architecture Specification

**Status:** Approved Architecture Specification  
**Review Date:** October 19, 2026  
**Reference Documents:**  
- [README.md](README.md)  
- [RESEARCH_FINDINGS.md](RESEARCH_FINDINGS.md)  
- [BASE_SELECTION.md](BASE_SELECTION.md)  
- [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md)  
- Test fixtures in `tests/fixtures/crux3.8/`

---

## 1. Core Architecture Boundary

```mermaid
graph TD
    UI["Qt 6 User Interface<br/>(MainWindow, PackageTreeView, TabDock, SearchBar)"]
    Ops["Application Operations Controller<br/>(TransactionPlanner, SearchFilterController, SettingsManager)"]
    Adapter["CRUX Backend Adapter (CruxAdapter)<br/>implements ICruxBackend"]
    Tools["Native CRUX Tools<br/>(prt-get, pkginfo, pkgmk, pkgadd, pkgrm, ports)"]
    PrivHelper["Privilege Elevation Helper<br/>(qt-sudo -d / sudo)"]

    UI -->|Qt Signals / Slots & DTOs| Ops
    Ops -->|ICruxBackend API| Adapter
    Adapter -->|Read-only queries (argv)| Tools
    Adapter -->|Mutations via qt-sudo (argv)| PrivHelper
    PrivHelper -->|Elevated child tree| Tools
```

### Architectural Responsibilities:
- **Qt UI:** Consumes strictly typed Data Transfer Objects (`PortInfo`, `TransactionPlan`, `BuildEvent`). It never invokes child processes, parses CLI text, or formats shell commands.
- **Application Operations:** Coordinates user intents, maintains UI view models (`PackageModel`), resolves filter predicates across the three data axes, and builds transaction queues.
- **CRUX Backend Adapter (`CruxAdapter`):** Owns native tool discovery, process invocation with explicit argument vectors, structured output tokenization, and filesystem inspection of port files. Cancellation sends `SIGTERM` to the directly spawned process only (the privilege helper); there is no process-group signalling, and whether the helper relays the signal to the root build tree is open test 2.10.3 (see §5.3).

---

## 2. Settled Decisions & Pending Validations

### 2.1 Settled Decisions
| Topic | Decision | Justification / Empirical Verification |
| :--- | :--- | :--- |
| **Upstream Base** | **Approach C (Clean-Room Modern Qt 6)** | Clean-room Qt 6 / C++17 core, backend adapter, and hand-crafted C++ UI widgets without brittle `.ui` XML files or `uic` code generation debt. |
| **Qt Version** | **Qt 6 (>= 6.2, target 6.12.0)** | Verified on target CRUX 3.8 (`pkgconfig-qt6core` version 6.12.0). Uses modern Qt 6 idioms without Qt 4/5 compatibility layers. |
| **Build System** | **CMake (>= 3.22, target 3.31.12)** | Verified on target CRUX 3.8. Standard CMake with automated `QTest` harness. |
| **Terminal / Logging** | **Headless QProcess Streaming with Optional PTY** | `qtermwidget6` is absent on standard CRUX installations. Streaming log events via `QProcess::readyReadStandardOutput/Error` is mandatory; external terminal emulator fallback supported for interactive builds. |
| **Privilege Model** | **Configurable Privilege Helper (Unprivileged GUI)** | GUI application is 100% unprivileged. `PrivilegeHelper` dynamically discovers/configures the elevation tool (`qt-sudo`, `sudo`, `doas`, `su`) and backend flags, never hardcoding `-d`. On the test machine sudoers had `(ALL : ALL) ALL` followed by `(ALL) NOPASSWD: ALL`; the last match wins, so qt-sudo never asked for a password. With the NOPASSWD line commented out, qt-sudo asks once per elevated invocation (OPEN_QUESTIONS 2.15). The helper's prompt therefore depends on local sudoers and is **not** a safety gate; the OctoCRUX transaction preview is. Cancelled or failed authentication is reported as `AuthenticationError`. |
| **Query Engine** | **prt-get printf** | 2,307 ports queried in **69 ms** without cache on CRUX 3.8. Delimiters (`\t`) are strictly preserved. Caching is unnecessary on modern SSD systems, but kept configurable for slower hardware. |
| **Footprint Override** | **pkgmk -in (Ignore NEW only)** | Narrower and safer than `-if`. Permits overriding unproblematic additions without blinding the package manager to missing critical files. |
| **Packaging Target** | **CRUX Pkgfile (.pkg.tar.zst)** | Package files in `/usr/ports/packages` use Zstandard compression (`PKGMK_COMPRESSION_MODE="zst"`). |

### 2.2 Pending Architectural Validations (SCOFS Protocol)
1. **Process Cancellation (Open Test 2.10.3):**
   * *Status:* **Verified for the common cases** (OPEN_QUESTIONS 2.15). With qt-sudo 2.4.1 and the sudo backend, Cancel stopped the whole root tree, both during a download and during a compilation (`configure` -> `gcc`/`cc1`): within about 1 s, with no orphans and nothing installed. pkgmk's `interrupted()` trap did not run, so its work directory stays behind. **[open]** The processes probably die of SIGPIPE, not of the relayed signal, so a long step that writes nothing might survive until its next write.
2. **Build Directory & `PKGMK_WORK_ROOT` (Conditional):**
   * *Status:* User-specific, not CRUX standard. The variable `PKGMK_WORK_ROOT` exists in the owner's custom `/etc/pkgmk.conf`, not in standard CRUX. Moreover, standard `sudo` resets environment variables (`env_reset`) and will strip `PKGMK_WORK_ROOT` unless explicitly in `env_keep`. OctoCRUX must not assume this variable exists; it should only inspect `/proc/self/mountinfo`, warn when `/usr/ports/work` tmpfs space is low, and classify `ENOSPC` errors cleanly at runtime.
3. **Package Signing & `signify` Errors:**
   * *Status:* Discovered in `/etc/pkgmk.conf` (`PKGMK_PRIVATEKEY="/home/dani77/.signify/d77crux.sec"`). Root builds read a private key in the user's home directory. OctoCRUX must classify signature failures as a dedicated error class.

---

## 3. Domain Model: The Three-Axis Port State

A port's status in OctoCRUX is evaluated across **three orthogonal axes**:

```
                  Axis 2: Lock Status (%l)
                     [yes]        [no]
                     +------------+------------+
        [yes]        | Installed  | Installed  |
                     | & Locked   | (Standard) |
                     +------------+------------+
Axis 1: [diff]       | Outdated   | Outdated   |
Status               | & Locked   | (Eligible) |
(%i)                 +------------+------------+
        [no]         | Locked     | Available  |
                     | Uninstalled| (Standard) |
                     +------------+------------+

Axis 3: Virtual Provider / Alias (from /etc/prt-get.aliases)
        Qualifies any port as "satisfied by <alias_package>"
        (e.g., rust is satisfied by installed rust-bin).
```

### The 6 Fundamental States:
1. **Installed (`yes` + `no`):** Port is installed and up-to-date with tree.
2. **Installed & Locked (`yes` + `yes`):** Port is installed, up-to-date, and explicitly locked against modifications.
3. **Outdated (`diff` + `no`):** Tree version differs from installed version; eligible for upgrade during `sysup`.
4. **Outdated & Locked (`diff` + `yes`):** Tree version differs, but package is locked. **Critical note:** Standard CLI `prt-get diff` hides these; OctoCRUX surfaces them with an explicit "Outdated (Locked)" indicator.
5. **Available (`no` + `no`):** Port is uninstalled and unlocked.
6. **Locked Uninstalled (`no` + `yes`):** Port is not installed, but locked in `/var/lib/pkg/prt-get.locker` to prevent accidental source compilation when a binary port is preferred (e.g. `rust` vs `rust-bin`).

---

## 4. Abstract Interface: `ICruxBackend`

The backend interface is purely abstract and independent of Qt widgets:

```cpp
namespace OctoCRUX {

enum class InstallStatus {
    NotInstalled,
    Installed,
    Outdated
};

enum class LockStatus {
    Unlocked,
    Locked
};

enum class OperationErrorKind {
    None,
    CompilationError,
    FootprintError,
    SignatureError,   // signify signing or key verification failure
    InstallError,     // pkgadd conflict or rejection
    DiskSpaceError    // ENOSPC in work dir
};

struct PortInfo {
    QString name;
    QString version;
    QString release;
    InstallStatus installStatus;
    LockStatus lockStatus;
    QString path;              // e.g. /usr/ports/core/glibc
    QString collection;        // deduced from path: core, opt, xorg, d77crux
    QString description;
    QStringList dependencies;  // direct dependencies from %e
    bool hasReadme;
    bool hasPreInstall;
    bool hasPostInstall;
    QString providedByAlias;   // e.g. "rust-bin" if satisfied via alias
};

struct InstalledPackageDetails {
    QString name;
    QString versionRelease;
    QStringList installedFiles;
};

struct TransactionPlan {
    QStringList toInstall;
    QStringList toUpdate;
    QStringList toRemove;
    QStringList recursiveInstallOrder;     // from quickdep
    QStringList reverseDependentsAffected; // from dependent (without --all)
    quint64 estimatedWorkSpaceAvailable;  // available bytes on work dir mount
};

class ICruxBackend : public QObject {
    Q_OBJECT
public:
    virtual ~ICruxBackend() = default;

    // --- Read-only queries ---
    virtual QList<PortInfo> queryAllPorts() = 0;
    virtual std::optional<PortInfo> queryPort(const QString &portName) = 0;
    virtual QStringList queryOutdatedPorts() = 0;
    virtual QStringList queryLockedPorts() = 0;
    virtual QMap<QString, QString> queryAliases() = 0;

    // --- Metadata inspection ---
    virtual QString readPortFile(const QString &portName, const QString &fileName) = 0; // Pkgfile, README, .footprint
    virtual QStringList queryFilesForInstalledPackage(const QString &packageName) = 0;
    virtual QString findPackageOwnerOfFile(const QString &filePath) = 0;

    // --- Dependency & impact analysis ---
    virtual QStringList queryDirectInstalledDependents(const QString &portName) = 0; // removal impact
    virtual QStringList queryAllDeclaredDependents(const QString &portName) = 0;    // navigation
    virtual QStringList queryRecursiveInstallOrder(const QString &portName) = 0;    // quickdep

    // --- Mutations & Execution ---
    virtual bool executeTransaction(const TransactionPlan &plan) = 0;
    virtual bool syncPortsTree() = 0;
    virtual bool lockPort(const QString &portName) = 0;
    virtual bool unlockPort(const QString &portName) = 0;
    virtual void cancelActiveOperation() = 0;

signals:
    void outputReceived(const QString &text, bool isError);
    void progressChanged(int current, int total, const QString &phase);
    void operationFinished(int exitCode, OperationErrorKind errorKind, const QString &summary);
    void rejectedConfigsDetected(const QStringList &rejectedFiles);
};

} // namespace OctoCRUX
```

---

## 5. Execution, Cancellation & Process Management

### 5.1 Subprocess Execution Rules
1. **Explicit Argument Vectors:** Never invoke commands via `/bin/sh -c "<string>"`. All executions use explicit `QStringList` arguments passed to `QProcess::start(program, arguments)`.
2. **Environment Isolation:** Compilation parameters (`CFLAGS`, `MAKEFLAGS`, `CCACHE_DIR`, `PATH`) are sourced from `/etc/pkgmk.conf`. They are never inherited from the graphical desktop user session.
3. **Mount Point & Capacity Monitoring:**
   - Rather than maintaining an arbitrary hardcoded list of "giant ports", the backend queries available space on `PKGMK_WORK_DIR`'s mount point (via `/proc/self/mountinfo` and `statvfs`) before executing.
   - If `ENOSPC` occurs, it is captured and categorized explicitly as `OperationErrorKind::DiskSpaceError`.
   - If the local `pkgmk.conf` provides a fallback mechanism (such as `PKGMK_WORK_ROOT`), it can be offered dynamically.

### 5.2 Privilege Escalation Flow
```
[User clicks Install / Upgrade / Upgrade All / Remove]
        |
        v
[TransactionPlanner builds the plan]  (no process)
        |
        v
[TransactionPreviewDialog]            (no process; Cancel is the default)
        | shows ICruxBackend::transactionCommands(plan): exact argv, in order
        | user presses Apply  -- Cancel / Esc / close => nothing runs
        v
[OctoCRUX UI (UID 1000)]
        | launches QProcess with configured helper & flags:
        | e.g. [qt-sudo, <detected_flag>, prt-get, depinst, <pkg>]
        v
[Privilege Helper (e.g. qt-sudo / sudo / doas)]
        | prompts for password if required (subject to sudoers rules)
        v
[Elevated Child Process (Root UID 0)]
        | spawns child process group
        v
[prt-get -> pkgmk -> make -> gcc] (writes to /var/cache/ccache, signs with signify)
```

#### 5.2.1 Transaction Preview (before any elevation)
- **Upgrade** acts only on the selected package and is enabled only when it is outdated and unlocked. **Upgrade All** is the system-wide upgrade, enabled whenever at least one package is outdated and unlocked, whatever is selected (`MainWindow::hasUpgradablePackages`). Button state is re-evaluated after every catalog reload, because reloading the model clears the selection without emitting `selectionChanged`.
- **Install, Upgrade, Upgrade All, Remove** always open `TransactionPreviewDialog` before anything runs. It shows:
  - the exact command lines in execution order, from `ICruxBackend::transactionCommands(plan)`. `CruxAdapter` builds them with the same function `executeTransaction()` uses (`buildBatchedOperations`), already wrapped by the privilege helper, so the preview cannot drift from what runs;
  - the number of elevated invocations. Whether each one asks for authentication is not verified, so the dialog says "may ask";
  - the ports to install, update, reinstall and remove, and the ports skipped because they are locked;
  - for removals, the installed dependents affected, both direct and the recursive cascade;
  - warnings: the plan's critical warnings, ports with `pre-install`/`post-install` scripts (`%E`/`%O`), and the work directory mount (tmpfs or not) with its free space, for operations that build.
  Cancel is the default button.
- A plan with no commands shows "Nothing to execute" with the locked ports, and starts nothing. (Upgrade and Upgrade All are already disabled when every outdated port is locked.)
- Core packages never reach the dialog: the plan is invalid, and the existing error box reports it.
- **Sync** (`ports -u`) uses a simple confirmation that shows the command; Cancel is the default. It rewrites the ports tree, not installed packages.
- **Lock/Unlock** run without confirmation (reversible with one click).
- Verified offscreen in `test_mainwindow` with a fake `prt-get`/`ports` first in `PATH` that logs every start. Rejecting the preview or the Sync confirmation starts no process; Apply runs exactly the previewed command once. The fakes must win the `PATH` lookup before any button is clicked.
- Later, optional: append the output of the matching `prt-get --test` command (unprivileged) to the preview.

#### 5.2.2 Per-package build check exceptions
- **Where:** the Information tab of each port, "Build checks for the next run of this package". The boxes are checked by default:
  - **Verify footprint.** Unchecked, it offers *Ignore new files only* (`pkgmk -in`, via `prt-get --margs=-in`), which is recommended and the default, or *Ignore all differences* (`-if`).
  - **Verify md5sum.** Unchecked: `-im`.
  - **Verify signature.** Unchecked: `-is`, with a red warning label.
- **Scope:** session only, never saved. An exception applies to the next Install / Upgrade / Upgrade All step that builds the package, including as a dependency. When that run ends (success, failure or cancel), the checks are on again. Rejecting the preview keeps the exception, because nothing ran.
- **Execution:** prt-get applies options to a whole invocation. `buildBatchedOperations` cuts each ordered target list into runs of consecutive ports with identical exceptions, one invocation each, so the quickdep order (dependencies first) is preserved. Every extra invocation is another elevation, and the preview counts them.
- **Preview:** it lists the exceptions per package. If any signature check is disabled, a red banner at the top names the packages.
- **Footprint retry:** after a run that was not cancelled, `BuildLogAnalyzer::extractFootprintFailures` parses its output.
  - Ports that failed with **NEW entries only** are offered "Retry with -in" (Cancel is the default). The retry goes through the normal preview.
  - Failures with **MISSING** entries are only reported in the Output, because `-in` cannot fix them, with a pointer to `-if` in the Information tab.
  - The offer does not depend on the exit code, because prt-get's exit code for a multi-port run with failures is not verified.
  - Verified in the sandbox: `pkgmk -in` turns a NEW-only mismatch into `New files found:` + `Building '...' succeeded.` (rc 0). The real criu and elfutils logs of 2026-10-20 parse as NEW-only.

### 5.3 Cancellation Policy (Pending Test 2.10.3)
- When the user clicks **Cancel**:
  1. OctoCRUX issues `SIGTERM` to the immediate child helper process via `CruxProcessRunner::cancel()` (`m_process->terminate()`), strictly avoiding blunt `SIGKILL`.
  2. **Crucial Verification Requirement [partially verified]:** Testing must verify whether the elevation wrapper (`qt-sudo` -> `sudo`) relays `SIGTERM` cleanly to the child `prt-get`/`pkgmk` process tree across UIDs, or whether killing `qt-sudo` leaves the root compilation running orphaned in the background. Real results (OPEN_QUESTIONS 2.15): Cancel during a download (`pkgmk` -> `curl`) and during a compilation (`configure` -> `gcc`) left no surviving root process. Open: a silent long-running step (SIGPIPE hypothesis), and the leftover pkgmk work directory.
  3. If signal relaying fails in testing, process group line discipline via an embedded pseudo-terminal (PTY) will be employed as the UID-independent cancellation mechanism.
  4. Never execute `killall`.
  5. **Stalled cancellation feedback:** if the process is still running `CruxProcessRunner::kDefaultCancelGracePeriodMs` (10 s, `setCancelGracePeriodMs()`) after the request, the runner emits a warning **as an ordinary error line on the output stream**, so it appears in red in the Output tab (and briefly in the status bar) like any other stderr line. It also emits `cancellationStalled(pid, elapsedMs)`, which nothing in the UI uses yet (no dialog, no dedicated indicator). It does **not** escalate to `SIGKILL`. The Cancel button stays enabled while the operation runs; pressing it again resends `SIGTERM` and restarts the grace period.
  6. **Window close during an operation:** `~CruxProcessRunner()` sends `SIGTERM`, waits 1 s, then destroys the `QProcess`, whose destructor kills (`SIGKILL`) a child that is still running. To keep that path out of normal use, `MainWindow::closeEvent` **refuses to close** while an operation is in progress and shows a window-modal message explaining why (owner decision, 2026-10-20). The destructor path is still reachable if the process is killed or the session ends forcibly; a logout while an operation runs is also refused by OctoCRUX.

### 5.4 Post-Transaction Health Checks
After any transaction completes:
1. **Rejected Files Check:** Inspect `/var/lib/pkg/rejected`. If files exist, emit `rejectedConfigsDetected()` so the UI alerts the user to run `rejmerge`.
2. **Failed Build Logs:** If a `sysup` exits with errors, inspect `/var/log/pkgbuild/*.log` to identify and display the specific failing port's compiler diagnostics.

---

### 5.5 Tray Notifier (`octocrux-notifier`, Phase 5c)
Adapted from the OctoXBPS notifier (`notifier/mainwindow.cpp`, commit `5f1e8b2`). OctoXBPS syncs on a schedule as root with `pkexec xbps-install -Sy`, without a password through a polkit rule restricted to that exact command; it checks every 5 minutes and offers once a day / once a day at H / every N minutes / never.
- **Read-only by default.** `NotifierCore` counts outdated packages from the backend's `prt-get printf` query (`UpdateSummary`: unlocked = what Upgrade All would update; locked counted apart). It refreshes every 5 minutes and when `/var/lib/pkg` changes (pkgadd's db, prt-get's locker), debounced by 2 s. `prt-get diff`/`printf` need no privileges; `ports -u` does.
- **No automatic sync by default** (`SyncMode::Never`). The tray shows "Ports tree last synced N days ago" and offers "Sync ports tree now", which runs `ports -u` through the privilege helper (it may ask for a password). The time of a successful sync, from the notifier or the main window's Sync, is stored in the shared settings (`OctoCruxSettings`, `~/.config/OctoCRUX/octoCRUX.conf`).
- **Scheduling** (`isSyncDue`, a pure function): the OctoXBPS modes with three deliberate differences.
  - The default is Never.
  - "Once a day at H" catches up later the same day if the machine was off at H.
  - The minimum interval is 60 minutes.
  The suggested mode when the user enables automatic sync is once a day, changeable in the GUI.
- **Automatic sync (step 2), only when the user picks a mode in Options:** `NotifierCore::tick()` runs every 5 minutes. When `isSyncDue` says so, the network is up (an interface that is up, not loopback, with an address, as in OctoXBPS) and no `prt-get`/`ports`/`pkgmk`/`pkgadd`/`pkgrm` is running (`/proc/*/comm`), it calls `UnattendedSync`, which never asks for a password:
  1. `pkcheck --action-id org.octocrux.ports-sync --process <pid>`, which is non-interactive. Exit 0 → `pkexec /usr/bin/ports -u`.
  2. Otherwise `sudo -n /usr/bin/ports -u`, which fails at once if sudoers wants a password.
  3. Otherwise nothing runs. The tray says "Automatic sync needs authorization (see Options)" and notifies once.
  A failed attempt postpones the next one by one schedule period.
- **Why a dedicated polkit action** (`contrib/polkit/org.octocrux.ports-sync.policy`, with `exec.path=/usr/bin/ports`, `exec.argv1=-u`, `allow_active=auth_admin`): pkexec has no non-interactive mode, so authorization must be checked first. polkit 128 refuses an unprivileged `pkcheck` on pkexec's generic action with details: "Only trusted callers (e.g. uid 0 or an action owner) can use CheckAuthorization() and pass details" **[verified]**. With its own action id, the check works without privileges. The exit code of `pkcheck` (`UnattendedSync::PolkitStatus`) tells the user why unattended sync is or is not authorized:
  - 0: authorized;
  - 2: an active session without the rule;
  - 1: polkit does not see a local active session, because the action refuses inactive subjects;
  - 127: the action is not registered.
  On the owner's T480s the answer is 1 **[verified 2026-10-21]**. ConsoleKit2 runs, but `seat0` has no sessions, because the X session was not started with `ck-launch-session`. The strict rule (`subject.local && subject.active`) therefore never applies there, and the `sudo -n` fallback does the scheduled syncs. The owner decided to keep the rule strict, not to relax it to the user only as OctoXBPS does with the group: relaxing it would also authorize, for example, an SSH session of that user. Registering the session (README, "Notifier and automatic sync") is the documented recommendation. That `exec.argv1` maps `pkexec /usr/bin/ports -u` to this action stays **[hypothesis]**; OctoXBPS relies on it.
- **Opt-in rule:** the Options dialog offers the rule when automatic sync is turned on, and offers to remove it when turned off. The rule is `/etc/polkit-1/rules.d/49-octocrux-ports-sync-<user>.rules` and returns `YES` for that action only, only for that user, and only in a local active session. It is installed through the privilege helper with `install -D -m 0644`, from a temporary file the notifier wrote; until packaging exists, the action definition is installed the same way. User names are validated before being embedded in the rule. On systems without polkit, `contrib/sudoers.d/octocrux-ports-sync.example` grants exactly `/usr/bin/ports -u` to one user.
- **Planned (step 3):** "Upgrade All…" opens OctoCRUX straight on the preview. An autostart `.desktop` is shipped but not enabled; each user turns it on.
- It never installs, upgrades or removes packages.

## 6. SCOFS Validation Ladder & Test Strategy

```
Level 5: Bare-metal CRUX 3.8 Validation
         Live GUI package builds, tmpfs quota alerts, and qt-sudo prompt.
Level 4: Supervised Disposable Container / Chroot
         Execute mutating depinst/update/remove and test SIGINT cancellation.
Level 3: Qt Model / View Integration Tests
         Test PackageModel filtering across 3 axes using mock ICruxBackend.
Level 2: Non-mutating Native Commands on CRUX
         Verify live prt-get printf against CruxAdapter on actual machine.
Level 1: QTest Unit Tests with Captured Fixtures
         Feed tests/fixtures/crux3.8/cmd/ dumps into parser classes:
         - test_printf_parser: parse printf-literaltab.out (2,307 ports).
         - test_locks_parser: parse prtget-listlocked.out and locker file.
         - test_quickdep_parser: parse quickdep whitespace & aliases.
         - test_dependent_parser: parse dependent vs dependent-all.
```
