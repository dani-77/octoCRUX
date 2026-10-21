#include <QTest>
#include "backend/privilege_helper.h"

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>

using namespace OctoCRUX;

class TestPrivilegeHelper : public QObject {
    Q_OBJECT

private slots:
    void testWrapQtSudo();
    void testWrapSudo();
    void testWrapDoas();
    void testWrapSu();
    void testWrapNone();
    void testHelperBinary();
    void testProbeAvailableHelper();
    void testFromBinaryName();
    void testResolvePreferredAndFallback();
};

void TestPrivilegeHelper::testWrapQtSudo() {
    PrivilegeHelper helper(PrivilegeHelperType::QtSudo);
    const QStringList wrapped = helper.wrap(QStringLiteral("prt-get"), {QStringLiteral("depinst"), QStringLiteral("gtk3")});

    // Exactly matches CRUX 3.8 qt-sudo syntax: qt-sudo -d <command> <args...>
    QCOMPARE(wrapped.size(), 5);
    QCOMPARE(wrapped.at(0), QStringLiteral("qt-sudo"));
    QCOMPARE(wrapped.at(1), QStringLiteral("-d"));
    QCOMPARE(wrapped.at(2), QStringLiteral("prt-get"));
    QCOMPARE(wrapped.at(3), QStringLiteral("depinst"));
    QCOMPARE(wrapped.at(4), QStringLiteral("gtk3"));
}

void TestPrivilegeHelper::testWrapSudo() {
    PrivilegeHelper helper(PrivilegeHelperType::Sudo);
    const QStringList wrapped = helper.wrap(QStringLiteral("ports"), {QStringLiteral("-u")});

    QCOMPARE(wrapped.size(), 3);
    QCOMPARE(wrapped.at(0), QStringLiteral("sudo"));
    QCOMPARE(wrapped.at(1), QStringLiteral("ports"));
    QCOMPARE(wrapped.at(2), QStringLiteral("-u"));
}

void TestPrivilegeHelper::testWrapDoas() {
    PrivilegeHelper helper(PrivilegeHelperType::Doas);
    const QStringList wrapped = helper.wrap(QStringLiteral("prt-get"), {QStringLiteral("lock"), QStringLiteral("llvm")});

    QCOMPARE(wrapped.size(), 4);
    QCOMPARE(wrapped.at(0), QStringLiteral("doas"));
    QCOMPARE(wrapped.at(1), QStringLiteral("prt-get"));
    QCOMPARE(wrapped.at(2), QStringLiteral("lock"));
    QCOMPARE(wrapped.at(3), QStringLiteral("llvm"));
}

void TestPrivilegeHelper::testWrapSu() {
    PrivilegeHelper helper(PrivilegeHelperType::Su);
    const QStringList wrapped = helper.wrap(QStringLiteral("prt-get"), {QStringLiteral("remove"), QStringLiteral("gtk3")});

    QCOMPARE(wrapped.size(), 3);
    QCOMPARE(wrapped.at(0), QStringLiteral("su"));
    QCOMPARE(wrapped.at(1), QStringLiteral("-c"));
    QCOMPARE(wrapped.at(2), QStringLiteral("'prt-get' 'remove' 'gtk3'"));

    // Verify protection against shell injection and variables
    const QStringList attack = helper.wrap(QStringLiteral("echo"), {QStringLiteral("$USER; rm -rf /"), QStringLiteral("it's cool")});
    QCOMPARE(attack.at(2), QStringLiteral("'echo' '$USER; rm -rf /' 'it'\\''s cool'"));
}

void TestPrivilegeHelper::testWrapNone() {
    PrivilegeHelper helper(PrivilegeHelperType::None);
    const QStringList wrapped = helper.wrap(QStringLiteral("prt-get"), {QStringLiteral("printf"), QStringLiteral("%n")});

    QCOMPARE(wrapped.size(), 3);
    QCOMPARE(wrapped.at(0), QStringLiteral("prt-get"));
    QCOMPARE(wrapped.at(1), QStringLiteral("printf"));
    QCOMPARE(wrapped.at(2), QStringLiteral("%n"));
}

void TestPrivilegeHelper::testHelperBinary() {
    QCOMPARE(PrivilegeHelper::helperBinary(PrivilegeHelperType::QtSudo), QStringLiteral("qt-sudo"));
    QCOMPARE(PrivilegeHelper::helperBinary(PrivilegeHelperType::Sudo), QStringLiteral("sudo"));
    QCOMPARE(PrivilegeHelper::helperBinary(PrivilegeHelperType::Doas), QStringLiteral("doas"));
    QCOMPARE(PrivilegeHelper::helperBinary(PrivilegeHelperType::Su), QStringLiteral("su"));
    QVERIFY(PrivilegeHelper::helperBinary(PrivilegeHelperType::None).isEmpty());
}

void TestPrivilegeHelper::testProbeAvailableHelper() {
    PrivilegeHelperType detected = PrivilegeHelper::probeAvailableHelper();
    // On the development host or test machine, probe must return a valid type
    QVERIFY(detected == PrivilegeHelperType::QtSudo ||
            detected == PrivilegeHelperType::Sudo ||
            detected == PrivilegeHelperType::Doas ||
            detected == PrivilegeHelperType::Su ||
            detected == PrivilegeHelperType::None);
}

void TestPrivilegeHelper::testFromBinaryName() {
    for (PrivilegeHelperType t : {PrivilegeHelperType::QtSudo, PrivilegeHelperType::Sudo,
                                  PrivilegeHelperType::Doas, PrivilegeHelperType::Su}) {
        QCOMPARE(PrivilegeHelper::fromBinaryName(PrivilegeHelper::helperBinary(t)), std::optional(t));
    }
    QVERIFY(!PrivilegeHelper::fromBinaryName(QStringLiteral("auto")).has_value());
    QVERIFY(!PrivilegeHelper::fromBinaryName(QString()).has_value());
    QVERIFY(!PrivilegeHelper::fromBinaryName(QStringLiteral("pkexec")).has_value());
}

void TestPrivilegeHelper::testResolvePreferredAndFallback() {
    // PATH holds only a stand-in "doas", so the lookup is the real QStandardPaths
    // one and no real helper on the host can win.
    QTemporaryDir bin;
    QVERIFY(bin.isValid());
    QVERIFY(QFile::link(QStringLiteral(OCTOCRUX_SOURCE_DIR "/tests/scripts/fake_sudo.sh"),
                        bin.filePath(QStringLiteral("doas"))));
    const QByteArray savedPath = qgetenv("PATH");
    auto restore = qScopeGuard([&]() { qputenv("PATH", savedPath); });
    qputenv("PATH", QFile::encodeName(bin.path()));

    PrivilegeHelper::Resolution r = PrivilegeHelper::resolve(PrivilegeHelperType::Doas);
    QCOMPARE(r.type, PrivilegeHelperType::Doas);
    QVERIFY(!r.fellBack);

    // Preferred helper missing: automatic detection, and the caller is told.
    r = PrivilegeHelper::resolve(PrivilegeHelperType::Sudo);
    QCOMPARE(r.type, PrivilegeHelperType::Doas);
    QVERIFY(r.fellBack);

    r = PrivilegeHelper::resolve(std::nullopt);
    QCOMPARE(r.type, PrivilegeHelperType::Doas);
    QVERIFY(!r.fellBack);

    qputenv("PATH", QFile::encodeName(QDir(bin.path()).filePath(QStringLiteral("empty"))));
    r = PrivilegeHelper::resolve(PrivilegeHelperType::QtSudo);
    QCOMPARE(r.type, PrivilegeHelperType::None);
    QVERIFY(r.fellBack);
}

QTEST_MAIN(TestPrivilegeHelper)
#include "test_privilege_helper.moc"
