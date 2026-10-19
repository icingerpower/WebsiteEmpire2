#include <QtTest>
#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "launcher/LauncherUpdate.h"
#include "gui/panes/UpdateStrategyTree.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageRepositoryDb.h"
#include "workingdirectory/WorkingDirectoryManager.h"

class Test_Website_LauncherUpdate_Links : public QObject
{
    Q_OBJECT
private slots:
    void test_launcherupdate_link_gate_data();
    void test_launcherupdate_link_gate();
    void test_launcherupdate_timeout_restarts_page_update();
};

void Test_Website_LauncherUpdate_Links::test_launcherupdate_link_gate_data()
{
    QTest::addColumn<QString>("mode");
    QTest::addColumn<int>("calls");
    QTest::addColumn<bool>("saved");
    QTest::newRow("three-dead-links-preserve-original") << QStringLiteral("reject") << 3 << false;
    QTest::newRow("corrected-retry-saves") << QStringLiteral("repair") << 2 << true;
    QTest::newRow("blocked-link-warns-and-saves") << QStringLiteral("blocked") << 1 << true;
    QTest::newRow("wrong-pubmed-host-preserves-original") << QStringLiteral("badhost") << 3 << false;
    QTest::newRow("wrong-pubmed-host-corrected-on-retry") << QStringLiteral("repair-host") << 2 << true;
}

void Test_Website_LauncherUpdate_Links::test_launcherupdate_link_gate()
{
    QFETCH(QString, mode);
    QFETCH(int, calls);
    QFETCH(bool, saved);
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const QDir dir(fixture.path());
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    connect(&server, &QTcpServer::newConnection, &server, [&server]() {
        while (server.hasPendingConnections()) {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
                const QByteArray request = socket->readAll();
                const int status = request.contains("/dead") ? 404 : request.contains("/blocked") ? 403 : 200;
                const QByteArray body = "<title>Study</title>Evidence";
                socket->write("HTTP/1.1 " + QByteArray::number(status)
                              + " Test\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        }
    });
    const auto writeFile = [&dir](const QString &name, const QByteArray &body) {
        QFile file(dir.filePath(name));
        return file.open(QIODevice::WriteOnly) && file.write(body) == body.size();
    };
    const QString prefix = QStringLiteral("[TITLE level=\"1\"]Article[/TITLE]\n")
                         + QString(2100, QLatin1Char('a'));
    const QString before = prefix + QStringLiteral(" Original article.");
    const QString link = QStringLiteral("[LINKFIX url=\"http://127.0.0.1:%1/%2\"]study[/LINKFIX]")
                            .arg(server.serverPort());
    const bool badHost = mode == QStringLiteral("badhost") || mode == QStringLiteral("repair-host");
    const bool repairs = mode == QStringLiteral("repair") || mode == QStringLiteral("repair-host");
    const QString first = prefix + (badHost
        ? QStringLiteral("[LINKFIX url='https://pubmed.ncbi.gov/2342128/']study[/LINKFIX]")
        : link.arg(mode == QStringLiteral("blocked") ? QStringLiteral("blocked") : QStringLiteral("dead")));
    const QString repaired = prefix + link.arg(QStringLiteral("good"));
    QVERIFY(writeFile(QStringLiteral("first.txt"), first.toUtf8()));
    QVERIFY(writeFile(QStringLiteral("repaired.txt"), repaired.toUtf8()));
    // A local executable substitutes only the CLI transport. The real launcher,
    // network gate, retry prompt, SQLite repository and update history all run.
    QVERIFY(writeFile(QStringLiteral("claude"), QByteArray(
        "#!/bin/sh\n"
        "n=0\n"
        "if [ -f \"$LINK_FIXTURE/count\" ]; then read -r n < \"$LINK_FIXTURE/count\"; fi\n"
        "n=$((n + 1))\n"
        "printf '%s' \"$n\" > \"$LINK_FIXTURE/count\"\n"
        "cat > \"$LINK_FIXTURE/prompt-$n\"\n"
        "if [ \"$LINK_REPAIR\" = yes ] && [ \"$n\" -gt 1 ]; then\n"
        "  cat \"$LINK_FIXTURE/repaired.txt\"\n"
        "else\n"
        "  cat \"$LINK_FIXTURE/first.txt\"\n"
        "fi\n")));
    QVERIFY(QFile::setPermissions(dir.filePath(QStringLiteral("claude")),
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    PageDb db(dir);
    PageRepositoryDb repo(db);
    const int page = repo.create(QStringLiteral("article"), QStringLiteral("/fixture"), QStringLiteral("en"));
    const QHash<QString, QString> original{{QStringLiteral("1_text"), before},
                                         {QStringLiteral("unrelated"), QStringLiteral("preserve me")}};
    repo.saveData(page, original);
    const QString updatedBefore = repo.findById(page)->updatedAt;
    UpdateStrategyTree strategies(dir);
    const QString strategy = strategies.addStrategy(QStringLiteral("Test"), QStringLiteral("article"), {}, false);
    const QString prompt = strategies.addPrompt(strategy, QStringLiteral("Test"), QStringLiteral("Improve readability."));

    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PATH"), fixture.path() + QLatin1Char(':') + env.value(QStringLiteral("PATH")));
    env.insert(QStringLiteral("LINK_FIXTURE"), fixture.path());
    env.insert(QStringLiteral("LINK_REPAIR"), repairs ? QStringLiteral("yes") : QStringLiteral("no"));
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), dir.filePath(QStringLiteral("config")));
    process.setProcessEnvironment(env);
    process.start(QCoreApplication::applicationFilePath(),
                  {QStringLiteral("--fixture"), fixture.path(), QStringLiteral("--cli"), QStringLiteral("Claude")});
    QVERIFY(process.waitForStarted());
    QTRY_COMPARE_WITH_TIMEOUT(process.state(), QProcess::NotRunning, 15000);
    const QByteArray log = process.readAllStandardOutput() + process.readAllStandardError();
    QCOMPARE(process.exitCode(), 0);
    QFile counter(dir.filePath(QStringLiteral("count")));
    QVERIFY2(counter.open(QIODevice::ReadOnly), log.constData());
    QCOMPARE(counter.readAll().toInt(), calls);
    const auto after = repo.loadData(page);
    QCOMPARE(after.value(QStringLiteral("unrelated")), QStringLiteral("preserve me"));
    QCOMPARE(!repo.lastUpdateAttemptAt(page, prompt).isEmpty(), saved);
    if (!saved) {
        QCOMPARE(after, original);
        QCOMPARE(repo.findById(page)->updatedAt, updatedBefore);
        QVERIFY2(log.contains("REJECT") && log.contains("skipping"), log.constData());
    } else {
        QCOMPARE(after.value(QStringLiteral("1_text")), repairs ? repaired : first);
    }
    if (calls > 1) {
        QFile feedback(dir.filePath(QStringLiteral("prompt-2")));
        QVERIFY(feedback.open(QIODevice::ReadOnly));
        const QByteArray text = feedback.readAll();
        if (badHost) {
            QVERIFY(text.contains("pubmed.ncbi.gov/2342128/"));
            QVERIFY(text.contains("pubmed.ncbi.nlm.nih.gov"));
        } else {
            QVERIFY(text.contains("/dead"));
        }
        QVERIFY(text.contains("application rejected"));
    } else {
        QVERIFY2(log.contains("WARN unverified link"), log.constData());
    }
}

void Test_Website_LauncherUpdate_Links::test_launcherupdate_timeout_restarts_page_update()
{
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const QDir dir(fixture.path());

    const auto writeFile = [&dir](const QString &name, const QByteArray &body) {
        QFile file(dir.filePath(name));
        return file.open(QIODevice::WriteOnly) && file.write(body) == body.size();
    };

    const QString prefix = QStringLiteral("[TITLE level=\"1\"]Article[/TITLE]\n")
                         + QString(2100, QLatin1Char('a'));
    const QString before = prefix + QStringLiteral(" Original article.");
    const QString validUpdated = prefix + QStringLiteral(" Updated article content successfully.");
    QVERIFY(writeFile(QStringLiteral("valid.txt"), validUpdated.toUtf8()));

    // Script hangs on call 1 (sleeps), then succeeds on call 2.
    QVERIFY(writeFile(QStringLiteral("claude"), QByteArray(
        "#!/bin/sh\n"
        "n=0\n"
        "if [ -f \"$LINK_FIXTURE/count\" ]; then read -r n < \"$LINK_FIXTURE/count\"; fi\n"
        "n=$((n + 1))\n"
        "printf '%s' \"$n\" > \"$LINK_FIXTURE/count\"\n"
        "if [ \"$n\" -eq 1 ]; then\n"
        "  sleep 10\n"
        "  exit 0\n"
        "fi\n"
        "cat \"$LINK_FIXTURE/valid.txt\"\n")));
    QVERIFY(QFile::setPermissions(dir.filePath(QStringLiteral("claude")),
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    PageDb db(dir);
    PageRepositoryDb repo(db);
    const int page = repo.create(QStringLiteral("article"), QStringLiteral("/timeout-page"), QStringLiteral("en"));
    repo.saveData(page, {{QStringLiteral("1_text"), before}});

    UpdateStrategyTree strategies(dir);
    const QString strategy = strategies.addStrategy(QStringLiteral("TestStrat"), QStringLiteral("article"), {}, false);
    const QString prompt = strategies.addPrompt(strategy, QStringLiteral("TestPrompt"), QStringLiteral("Improve content."));

    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PATH"), fixture.path() + QLatin1Char(':') + env.value(QStringLiteral("PATH")));
    env.insert(QStringLiteral("LINK_FIXTURE"), fixture.path());
    env.insert(QStringLiteral("TEST_TIMEOUT_MS"), QStringLiteral("300"));
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), dir.filePath(QStringLiteral("config")));
    process.setProcessEnvironment(env);
    process.start(QCoreApplication::applicationFilePath(),
                  {QStringLiteral("--fixture"), fixture.path(), QStringLiteral("--cli"), QStringLiteral("Claude")});
    QVERIFY(process.waitForStarted());
    QTRY_COMPARE_WITH_TIMEOUT(process.state(), QProcess::NotRunning, 15000);
    const QByteArray log = process.readAllStandardOutput() + process.readAllStandardError();
    QCOMPARE(process.exitCode(), 0);

    QFile counter(dir.filePath(QStringLiteral("count")));
    QVERIFY2(counter.open(QIODevice::ReadOnly), log.constData());
    // Must have made 2 calls (first timed out, second restarted and succeeded)
    QCOMPARE(counter.readAll().toInt(), 2);

    const auto after = repo.loadData(page);
    QCOMPARE(after.value(QStringLiteral("1_text")), validUpdated);
    QVERIFY(!repo.lastUpdateAttemptAt(page, prompt).isEmpty());
    QVERIFY2(log.contains("Timed out waiting for Claude"), log.constData());
    QVERIFY2(log.contains("Restarting page update for /timeout-page"), log.constData());
    QVERIFY2(log.contains("OK: /timeout-page"), log.constData());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() > 2 && args.at(1) == QStringLiteral("--fixture")) {
        WorkingDirectoryManager::instance()->open(args.at(2));
        if (qEnvironmentVariableIsSet("TEST_TIMEOUT_MS")) {
            const int ms = qEnvironmentVariable("TEST_TIMEOUT_MS").toInt();
            LauncherUpdate::setSubprocessTimeoutMsForTest(ms);
            LauncherUpdate::setHeartbeatMsForTest(ms / 3 > 10 ? ms / 3 : 10);
        }
        LauncherUpdate launcher;
        launcher.run({});
        return app.exec();
    }
    Test_Website_LauncherUpdate_Links test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_launcher_update_links.moc"
