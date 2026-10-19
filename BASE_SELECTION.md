# OctoCRUX — Base Selection & Architectural Strategy

**Status:** Proposed Recommendation  
**Review Date:** October 19, 2026  
**Reference Documents:**  
- [README.md](README.md)  
- [ARCHITECTURE.md](ARCHITECTURE.md)  
- [RESEARCH_FINDINGS.md](RESEARCH_FINDINGS.md)  

---

## 1. Context & Objectives

The primary goal of OctoCRUX is to deliver a reliable, responsive, and minimalist Qt 6 graphical frontend for CRUX Linux's native package and ports tooling (`pkgutils`, `prt-get`, `ports`). It must preserve the familiar Octopi-style user experience while respecting CRUX's source-based workflow and adhering strictly to the constraints outlined in [ARCHITECTURE.md](ARCHITECTURE.md):
- Strict architectural boundary: `Qt UI -> Application Operations -> CRUX Backend Adapter -> Native CRUX Tools`.
- No interpolation of untrusted input into shell strings.
- Non-blocking, real-time streaming of long-running compiler (`pkgmk`) output.
- Safe process group termination (`SIGINT`/`SIGTERM`) without naive `killall`.
- Non-intrusive, configurable privilege escalation without hardcoded desktop environments or tools.
- Independent testability of backend adapters and UI layers.

---

## 2. Evaluation of Candidate Approaches

Based on the evidence gathered in [RESEARCH_FINDINGS.md](RESEARCH_FINDINGS.md), three distinct development approaches were evaluated.

### Approach A: Direct Fork and Modification of OctoXBPS (or OctoPkg)

In this approach, the existing OctoXBPS codebase (itself a fork of OctoPkg/Octopi) is cloned into OctoCRUX, and its XBPS-specific execution logic (`xbpsexec.cpp`, `unixcommand.cpp`) is incrementally adapted to invoke CRUX commands.

* **Advantages:**
  - Fast visual prototype: the application builds and runs a pre-assembled Octopi window immediately.
  - Pre-existing wiring for menu actions, toolbars, and options dialogs.

* **Disadvantages & Critical Risks:**
  - **Severe Technical Debt:** OctoXBPS and OctoPkg were created by copy-pasting Octopi code without architectural refactoring. As documented in `RESEARCH_FINDINGS.md`, OctoXBPS contains orphaned variables named `pacman`, dead calls to FreeBSD's `pkg`, terminal-width hacks (`COLUMNS=170`), and hardcoded lockfile removal routines (`doRemovePacmanLockFile()`).
  - **Monolithic Coupling:** Package manager calls and stdout scraping are entangled across `MainWindow`, `MainWindow_Transaction`, `Package`, `PackageRepository`, and `UnixCommand`. There is no backend boundary.
  - **Inherent Security & Safety Flaws:** Subprocesses are assembled via string concatenation (e.g. `UnixCommand::getXBPSInstallBin() + " -y " + listOfPackages`). Cancellation relies on uncoordinated signals or `killall`.
  - **Hardcoded Dependencies:** Elevation hardcodes `/usr/local/bin/qt-sudo -d`, failing on standard CRUX systems where `sudo` or `doas` are used.
  - **Refactoring Trap:** Cleaning up this debt while keeping the UI functional would require more engineering effort than writing a clean implementation, with zero test coverage to guard against regressions.

### Approach B: Hybrid Model — Clean-Room Core & Backend with Reused Octopi UI Layout Assets

In this approach, OctoCRUX adopts a clean-room C++17 / Qt 6 backend architecture built with CMake, but reuses the UI layout files (`mainwindow.ui`, `transactiondialog.ui`), icon assets, and visual presentation idioms from Octopi/OctoXBPS.

* **Advantages:**
  - **Octopi Experience Preserved:** Retains the familiar, tested Octopi layout (toolbar, status bar, package tree view, tabbed info dock with Details, Files, Pkgfile, README, Footprint, and Output/Terminal).
  - **Architectural Purity:** Replaces the entire underlying implementation (`UnixCommand`, `Package`, `XBPSExec`) with a dedicated, modular CRUX backend adapter (`CruxAdapter`) complying strictly with `ARCHITECTURE.md`.
  - **Modern Qt 6 & CMake Foundation:** Built from the start using C++17, CMake, and standard Qt 6 types, avoiding legacy Qt 4/5 hacks and deprecated APIs.
  - **Deterministic Testing:** The backend adapter, command parser, and data models can be thoroughly covered by automated `QTest` suites using captured CRUX fixtures before any widget is connected.
  - **Safe Execution & Privileges:** Employs explicit argument vectors (`QStringList`), process-group signal routing, and configurable privilege elevation (`sudo`, `doas`, `su`, or `qt-sudo`).

* **Disadvantages:**
  - Requires writing clean Qt models (`QAbstractItemModel`) and connecting the UI forms from scratch.

### Approach C: 100% Clean-Room Qt 6 Implementation (Custom UI + Clean Backend)

In this approach, both the user interface and the backend adapter are designed and implemented completely from scratch in Qt 6 / C++17, without using Octopi's `.ui` files or code assets.

* **Advantages:**
  - Total control over widget architecture, styling, and ergonomics.
  - Opportunity to tailor the UI specifically to CRUX concepts (e.g., ports collections hierarchy, explicit source compilation logs, dual ports tree vs installed package view).
  - Zero licensing encumbrance from Octopi (only original code, licensed as desired).

* **Disadvantages:**
  - Higher initial development effort required to recreate complex UI controls (filter bars, package tree delegates, tab widgets, status icons).
  - May diverge from the familiar Octopi look-and-feel that CRUX users accustomed to Octopi expect.

---

## 3. Comparison & Tradeoff Matrix

| Evaluation Criterion | Approach A: Direct Fork (OctoXBPS) | Approach B: Hybrid (Clean Core + Octopi UI Assets) | Approach C: Pure Clean-Room |
| :--- | :--- | :--- | :--- |
| **Architectural Boundary** | ❌ None (deeply coupled) | ✅ Strict (`Qt UI -> Adapter -> CRUX tools`) | ✅ Strict (`Qt UI -> Adapter -> CRUX tools`) |
| **Technical Debt & Legacy Bugs** | ❌ Extreme (dead pacman/pkg code) | ✅ Zero | ✅ Zero |
| **Command Execution Safety** | ❌ Unsafe (string concatenation) | ✅ Safe (explicit argument vectors) | ✅ Safe (explicit argument vectors) |
| **Process Cancellation** | ❌ Broken / missing / `killall` | ✅ Process group (`SIGINT`/`SIGTERM`) | ✅ Process group (`SIGINT`/`SIGTERM`) |
| **CRUX Source Build Support** | ❌ Poor (assumes binary packages) | ✅ Native (`prt-get printf`, PTY compiler logs) | ✅ Native (`prt-get printf`, PTY compiler logs) |
| **Configurable Privileges** | ❌ Hardcoded `/usr/local/bin/qt-sudo` | ✅ Configurable (`sudo`/`doas`/`su`) | ✅ Configurable (`sudo`/`doas`/`su`) |
| **Testability (Unit/Integration)** | ❌ Untestable without UI | ✅ Highly testable via mock adapters & fixtures | ✅ Highly testable via mock adapters & fixtures |
| **Octopi Look & Feel** | ✅ Exact | ✅ High (reused `.ui` forms & icons) | ⚠️ Custom recreation |
| **Initial Implementation Effort** | Low to build, Very High to stabilize | Moderate, predictable | High UI effort |
| **Long-Term Maintainability** | ❌ Fragile | ✅ High | ✅ High |

---

## 4. Disqualification of Approach A (Direct Fork)

Directly forking OctoXBPS or OctoPkg is **formally rejected** for the following verified reasons:
1. **Architectural Violation:** It directly contradicts [ARCHITECTURE.md](ARCHITECTURE.md), which mandates a modular backend adapter and clean separation from UI widgets.
2. **Hidden Vulnerabilities:** Re-implementing package installation on top of `XBPSExec` or `UnixCommand` would carry over unescaped shell concatenation, whitespace parsing bugs, and terminal dimension hacks (`COLUMNS=170`).
3. **CRUX Workflow Incompatibility:** CRUX builds packages from source via `pkgmk`. Neither OctoPkg nor OctoXBPS has facilities for displaying lengthy, multi-hour compilation logs, parsing build errors, or cleanly cancelling compilation trees without corrupting the build directory or running destructive `killall` commands.

---

## 5. Recommendation: Approach B (Hybrid Strategy)

We recommend adopting **Approach B: Hybrid Clean-Room Core with Octopi UI Assets**.

### 5.1 Architectural Blueprint
The project will be structured in three decoupled modules:

```
+-------------------------------------------------------------------+
|                            OctoCRUX UI                            |
| (MainWindow, SearchBar, PackageTreeView, Tabbed Info & Log Panel) |
+---------------------------------+---------------------------------+
                                  | Qt Signals / Slots & DTOs
                                  v
+-------------------------------------------------------------------+
|                      Application Operations                       |
|   (TransactionPlanner, SearchFilterController, SettingsManager)   |
+---------------------------------+---------------------------------+
                                  | Abstract Interface: ICruxBackend
                                  v
+-------------------------------------------------------------------+
|                       CRUX Backend Adapter                        |
|  - QueryEngine: prt-get printf (%n\t%v\t%r\t%i\t%p\t%d...)        |
|  - FileReader: direct inspection of Pkgfile, README, .footprint    |
|  - SubprocessExecutor: QProcess / PTY with SIGINT process groups   |
|  - PrivilegeHelper: configurable elevation (sudo, doas, su)       |
+---------------------------------+---------------------------------+
                                  | Explicit argv execve
                                  v
+-------------------------------------------------------------------+
|                         Native CRUX Tools                         |
|             (prt-get, pkginfo, pkgmk, pkgadd, pkgrm, ports)       |
+-------------------------------------------------------------------+
```

### 5.2 Key Design Principles for the Recommended Base
1. **Build System & Toolchain:**
   - Standard CMake (>= 3.22) targeting C++17.
   - Qt 6 (Core, Gui, Widgets).
   - Optional dependency: `qtermwidget6` (for embedded interactive PTY terminal); if not installed, gracefully fall back to internal non-blocking log stream or configured external terminal emulator.
2. **Backend Adapter Interface (`ICruxBackend`):**
   - Read-only operations (`getAvailablePorts()`, `getInstalledPackages()`, `searchPorts()`, `getPortDetails()`, `getDependencies()`, `getReverseDependencies()`, `getOutdatedPackages()`).
   - Mutation operations (`installPort()`, `updatePort()`, `removePackage()`, `syncPortsTree()`).
   - Every operation returns structured data structures (e.g. `PortInfo`, `PackageInfo`, `TransactionPlan`) or streams asynchronous events (`outputReceived`, `progressChanged`, `operationFinished`).
3. **CRUX Tooling Invocations:**
   - Querying: `prt-get printf "%n\t%v\t%r\t%i\t%p\t%d\t%R\t%e\n"` parsed via tab separation.
   - Filesystem metadata: Read `Pkgfile`, `README`, `.footprint` directly from disk using the directory provided by `prt-get path <port>`.
   - Outdated detection: `prt-get diff`.
   - Mutation transactions: `prt-get depinst` / `prt-get update` / `pkgrm` executed with explicit argument vectors.
4. **Licensing & Attribution:**
   - OctoCRUX will be licensed under **GNU General Public License v2 (GPL-2.0)**.
   - Preserves upstream copyright attributions to Alexandre Albuquerque Arnt (Octopi UI layouts and icons) and Juergen Daubert (YAPO CRUX concepts).

---

## 6. Proposed Implementation Roadmap (Post-Research)

Once the research phase and architecture plan are approved by the owner:
1. **Phase 1: Test Harness & Mock Fixtures**
   - Create unit tests with real recorded outputs of `prt-get printf`, `pkginfo -i`, `prt-get diff`, and malformed/edge-case fixtures.
2. **Phase 2: Core CRUX Backend Adapter (`CruxAdapter`)**
   - Implement read-only queries, file readers, and argument vector execution.
   - Validate 100% green tests on parser and process execution logic.
3. **Phase 3: Read-Only Qt 6 Prototype**
   - Wire `CruxAdapter` to `PackageModel` and `MainWindow`.
   - Implement search, filtering (All, Installed, Outdated), and tab views (Details, Files, Pkgfile, README, Footprint).
4. **Phase 4: Transactions, Compilation Streaming & Privilege Escalation**
   - Implement transaction planning dialog.
   - Stream compiler output in real time with process group cancellation.
   - Add configurable privilege helper execution.
5. **Phase 5: Packaging & Real-World CRUX Validation**
   - Create CRUX `Pkgfile` for OctoCRUX.
   - Conduct validation on bare-metal CRUX installation.
