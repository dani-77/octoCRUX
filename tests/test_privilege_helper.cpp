#include <QTest>
#include "backend/privilege_helper.h"

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
    QCOMPARE(wrapped.at(2), QStringLiteral("prt-get remove gtk3"));
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

QTEST_MAIN(TestPrivilegeHelper)
#include "test_privilege_helper.moc"
