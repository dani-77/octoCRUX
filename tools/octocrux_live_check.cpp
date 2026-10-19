#include "backend/crux_adapter.h"
#include <QCoreApplication>
#include <QTextStream>
#include <iostream>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    out << "========================================================\n";
    out << "  OctoCRUX Level 2 Live Read-Only Sanity Check (CRUX)    \n";
    out << "========================================================\n\n";

    auto adapter = std::make_unique<OctoCRUX::CruxAdapter>();

    out << "[1/4] Running live queryAllPorts() via native prt-get...\n";
    out.flush();
    auto ports = adapter->queryAllPorts();
    out << "      -> Total ports parsed: " << ports.size() << "\n";

    int installed = 0;
    int outdated = 0;
    int locked = 0;
    int virtualAliases = 0;

    for (const auto &p : ports) {
        if (p.isInstalled()) installed++;
        if (p.installStatus == OctoCRUX::InstallStatus::Outdated) outdated++;
        if (p.lockStatus == OctoCRUX::LockStatus::Locked) locked++;
        if (!p.providedByAlias.isEmpty()) virtualAliases++;
    }

    out << "      -> Installed: " << installed << "\n";
    out << "      -> Outdated:  " << outdated << "\n";
    out << "      -> Locked:    " << locked << "\n";
    out << "      -> Satisfied via alias: " << virtualAliases << "\n\n";

    out << "[2/4] Testing queryAliases()...\n";
    auto aliases = adapter->queryAliases();
    for (auto it = aliases.constBegin(); it != aliases.constEnd(); ++it) {
        out << "      -> " << it.key() << " => " << it.value() << "\n";
    }
    out << "\n";

    out << "[3/4] Testing queryDirectInstalledDependents(\"gtk3\")...\n";
    out.flush();
    auto direct = adapter->queryDirectInstalledDependents(QStringLiteral("gtk3"));
    out << "      -> Count: " << direct.size() << "\n";
    out << "      -> Packages: " << direct.join(QStringLiteral(", ")) << "\n\n";

    out << "[4/4] Testing findPackageOwnerOfFile()...\n";
    out.flush();
    auto ownerGxx = adapter->findPackageOwnerOfFile(QStringLiteral("/usr/bin/g++"));
    out << "      -> Owner of /usr/bin/g++: '" << ownerGxx << "'\n";

    auto ownerPrt = adapter->findPackageOwnerOfFile(QStringLiteral("/usr/bin/prt-get"));
    out << "      -> Owner of /usr/bin/prt-get: '" << ownerPrt << "'\n\n";

    out << "========================================================\n";
    out << "  Level 2 Verification Finished Successfully!           \n";
    out << "========================================================\n";
    out.flush();

    return 0;
}
