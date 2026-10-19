#include <QtTest>

#include "AbstractLauncher.h"
#include "aicli/AbstractCli.h"

class Test_Website_LauncherCliSelection : public QObject
{
    Q_OBJECT

private slots:
    void test_launcher_cli_selection_data();
    void test_launcher_cli_selection();
    void test_launcher_cli_omitted_uses_default();
    void test_launcher_cli_invalid_does_not_fall_back_data();
    void test_launcher_cli_invalid_does_not_fall_back();
};

void Test_Website_LauncherCliSelection::test_launcher_cli_selection_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("executable");
    QTest::newRow("lowercase-codex") << QStringLiteral("codex") << QStringLiteral("codex");
    QTest::newRow("display-name") << QStringLiteral("Codex") << QStringLiteral("codex");
    QTest::newRow("mixed-case") << QStringLiteral("cOdEx") << QStringLiteral("codex");
    QTest::newRow("claude") << QStringLiteral("claude") << QStringLiteral("claude");
    QTest::newRow("antigravity-name") << QStringLiteral("Antigravity") << QStringLiteral("agy");
    QTest::newRow("antigravity-executable") << QStringLiteral("agy") << QStringLiteral("agy");
}

void Test_Website_LauncherCliSelection::test_launcher_cli_selection()
{
    QFETCH(QString, name);
    QFETCH(QString, executable);
    QString error = QStringLiteral("previous error");
    AbstractCli *cli = AbstractLauncher::resolveCli(
        {QStringLiteral("WebsiteEmpire"), QStringLiteral("--generation"),
         QStringLiteral("--cli"), name, QStringLiteral("--limit"), QStringLiteral("1")}, error);
    QVERIFY2(cli, qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(cli->getExecutable(), executable);
}

void Test_Website_LauncherCliSelection::test_launcher_cli_omitted_uses_default()
{
    QString error;
    AbstractCli *cli = AbstractLauncher::resolveCli(
        {QStringLiteral("WebsiteEmpire"), QStringLiteral("--generation")}, error);
    QVERIFY2(cli, qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(cli, AbstractCli::ALL_CLIS().first());
}

void Test_Website_LauncherCliSelection::test_launcher_cli_invalid_does_not_fall_back_data()
{
    QTest::addColumn<QStringList>("args");
    QTest::addColumn<QString>("errorFragment");
    QTest::newRow("unknown")
        << QStringList{QStringLiteral("--cli"), QStringLiteral("codxe")}
        << QStringLiteral("codxe");
    QTest::newRow("missing") << QStringList{QStringLiteral("--cli")}
                              << QStringLiteral("Missing value");
    QTest::newRow("empty") << QStringList{QStringLiteral("--cli"), QString()}
                            << QStringLiteral("Missing value");
    QTest::newRow("next-option")
        << QStringList{QStringLiteral("--cli"), QStringLiteral("--limit"), QStringLiteral("1")}
        << QStringLiteral("Missing value");
}

void Test_Website_LauncherCliSelection::test_launcher_cli_invalid_does_not_fall_back()
{
    QFETCH(QStringList, args);
    QFETCH(QString, errorFragment);
    QString error;
    QVERIFY(!AbstractLauncher::resolveCli(args, error));
    QVERIFY2(error.contains(errorFragment), qPrintable(error));
}

QTEST_APPLESS_MAIN(Test_Website_LauncherCliSelection)
#include "test_launcher_cli_selection.moc"
