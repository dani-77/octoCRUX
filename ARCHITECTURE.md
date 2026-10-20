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
| **Privilege Model** | **Configurable Privilege Helper (Unprivileged GUI)** | GUI application is 100% unprivileged. `PrivilegeHelper` dynamically discovers/configures the elevation tool (`qt-sudo`, `sudo`, `doas`, `su`) and backend flags, never hardcoding `-d`. Target CRUX system currently has `(ALL) NOPASSWD: ALL` in sudoers; password prompt paths must be verified in an unprivileged configuration without `NOPASSWD`. |
| **Query Engine** | **prt-get printf** | 2,307 ports queried in **69 ms** without cache on CRUX 3.8. Delimiters (`\t`) are strictly preserved. Caching is unnecessary on modern SSD systems, but kept configurable for slower hardware. |
| **Footprint Override** | **pkgmk -in (Ignore NEW only)** | Narrower and safer than `-if`. Permits overriding unproblematic additions without blinding the package manager to missing critical files. |
| **Packaging Target** | **CRUX Pkgfile (.pkg.tar.zst)** | Package files in `/usr/ports/packages` use Zstandard compression (`PKGMK_COMPRESSION_MODE="zst"`). |

### 2.2 Pending Architectural Validations (SCOFS Protocol)
1. **Process Cancellation (Open Test 2.10.3):**
   * *Status:* Unverified. While `qt-sudo` runs as the desktop user UID, it is untested whether `qt-sudo` cleanly forwards `SIGTERM`/`SIGINT` down to the child `sudo` process group, or whether killing `qt-sudo` leaves the root compilation tree running orphaned in the background. Must be validated in a disposable environment.
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
[User clicks Apply]
        |
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

### 5.3 Cancellation Policy (Pending Test 2.10.3)
- When the user clicks **Cancel**:
  1. OctoCRUX issues `SIGTERM` to the immediate child helper process via `CruxProcessRunner::cancel()` (`m_process->terminate()`), strictly avoiding blunt `SIGKILL`.
  2. **Crucial Verification Requirement [open]:** Testing must verify whether the elevation wrapper (`qt-sudo` -> `sudo`) relays `SIGTERM` cleanly to the child `prt-get`/`pkgmk` process tree across UIDs, or whether killing `qt-sudo` leaves the root compilation running orphaned in the background. This remains an open question until validated on disposable bare-metal or container environments.
  3. If signal relaying fails in testing, process group line discipline via an embedded pseudo-terminal (PTY) will be employed as the UID-independent cancellation mechanism.
  4. Never execute `killall`.
  5. **Stalled cancellation feedback:** if the process is still running `CruxProcessRunner::kDefaultCancelGracePeriodMs` (10 s, `setCancelGracePeriodMs()`) after the request, the runner emits a warning **as an ordinary error line on the output stream**, so it appears in red in the Output tab (and briefly in the status bar) like any other stderr line. It also emits `cancellationStalled(pid, elapsedMs)`, which nothing in the UI uses yet (no dialog, no dedicated indicator). It does **not** escalate to `SIGKILL`. The Cancel button stays enabled while the operation runs; pressing it again resends `SIGTERM` and restarts the grace period.
  6. **Known gap [open]:** `~CruxProcessRunner()` sends `SIGTERM`, waits 1 s, then destroys the `QProcess`, whose destructor kills (`SIGKILL`) a child that is still running. Closing OctoCRUX during an operation can therefore still `SIGKILL` the helper. The intended behaviour on window close (refuse, warn, or detach) is an owner decision.

### 5.4 Post-Transaction Health Checks
After any transaction completes:
1. **Rejected Files Check:** Inspect `/var/lib/pkg/rejected`. If files exist, emit `rejectedConfigsDetected()` so the UI alerts the user to run `rejmerge`.
2. **Failed Build Logs:** If a `sysup` exits with errors, inspect `/var/log/pkgbuild/*.log` to identify and display the specific failing port's compiler diagnostics.

---

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
