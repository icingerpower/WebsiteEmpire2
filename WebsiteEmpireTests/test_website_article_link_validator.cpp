#include <QtTest>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include "website/ArticleLinkValidator.h"

// Inject transport errors deterministically; successful requests still use the
// real loopback server. No dependency on a public DNS resolver or its cache.
class TransportErrorReply : public QNetworkReply
{
public:
    TransportErrorReply(const QNetworkRequest &request, NetworkError error, QObject *parent);
    void abort() override;
protected:
    qint64 readData(char *, qint64) override;
};

TransportErrorReply::TransportErrorReply(const QNetworkRequest &request,
                                       NetworkError error, QObject *parent)
    : QNetworkReply(parent)
{
    setRequest(request);
    setUrl(request.url());
    open(QIODevice::ReadOnly);
    QTimer::singleShot(0, this, [this, error]() {
        if (!isFinished()) {
            setError(error, QStringLiteral("Injected transport failure"));
            setFinished(true);
            emit errorOccurred(error);
            emit finished();
        }
    });
}

void TransportErrorReply::abort()
{
    if (!isFinished()) {
        setError(OperationCanceledError, QStringLiteral("Cancelled"));
        setFinished(true);
        emit finished();
    }
}

qint64 TransportErrorReply::readData(char *, qint64)
{
    return -1;
}

class TransportErrorNetwork : public QNetworkAccessManager
{
public:
    QList<QNetworkReply::NetworkError> failures;
    int requests = 0;
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData) override;
};

QNetworkReply *TransportErrorNetwork::createRequest(Operation operation,
                                                   const QNetworkRequest &request,
                                                   QIODevice *outgoingData)
{
    ++requests;
    if (!failures.isEmpty()) {
        return new TransportErrorReply(request, failures.takeFirst(), this);
    }
    return QNetworkAccessManager::createRequest(operation, request, outgoingData);
}

// Real loopback HTTP responses: tests never depend on public websites, DNS,
// medical content, or the availability of a paid CLI.
class Test_Website_ArticleLinkValidator : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void test_linkvalidator_destination_data();
    void test_linkvalidator_destination();
    void test_linkvalidator_quotes_duplicates_cache_and_relative_links();
    void test_linkvalidator_malformed_data();
    void test_linkvalidator_malformed();
    void test_linkvalidator_inconclusive_results_are_rechecked();
    void test_linkvalidator_cancellation_rejects();
    void test_linkvalidator_total_budget_is_bounded();
    void test_linkvalidator_known_bad_pubmed_host();
    void test_linkvalidator_dns_retry_data();
    void test_linkvalidator_dns_retry();
    void test_linkvalidator_dns_rejection_is_not_cached();
    void test_linkvalidator_bad_host_after_budget_exhausted();
    void test_linkvalidator_dns_retry_budget_and_cancellation_data();
    void test_linkvalidator_dns_retry_budget_and_cancellation();
    void test_linkvalidator_correct_pubmed_host_preserves_warning_policy();

private:
    QString link(const QString &path) const;
    QTcpServer m_server;
    QHash<QByteArray, int> m_hits;
};

void Test_Website_ArticleLinkValidator::init()
{
    m_hits.clear();
    QVERIFY(m_server.listen(QHostAddress::LocalHost, 0));
    connect(&m_server, &QTcpServer::newConnection, this, [this]() {
        while (m_server.hasPendingConnections()) {
            QTcpSocket *socket = m_server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("answered").toBool()) {
                    return;
                }
                socket->setProperty("answered", true);
                const QByteArray path = request.split(' ').value(1);
                ++m_hits[path];
                if (path.startsWith("/timeout")) {
                    return;
                }
                int code = 200;
                QByteArray extra;
                QByteArray body = "<html><title>A real medical study</title>Some text mentioning 404.</html>";
                if (path == "/redirect" || path == "/redirect-dead" || path == "/loop") {
                    code = 302;
                    extra = "Location: " + QByteArray(path == "/redirect" ? "/ok" :
                                                    path == "/redirect-dead" ? "/404" : "/loop") + "\r\n";
                } else if (path == "/soft404") {
                    body = "<title>Page Not Found - Site Help</title>";
                } else if (path == "/captcha") {
                    body = "<title>Checking your browser - reCAPTCHA</title>";
                } else if (path == "/empty") {
                    body.clear();
                } else if (path == "/large") {
                    body += QByteArray(256 * 1024, 'a');
                } else {
                    bool numeric = false;
                    const int requestedCode = path.mid(1).toInt(&numeric);
                    if (numeric) {
                        code = requestedCode;
                    }
                }
                socket->write("HTTP/1.1 " + QByteArray::number(code) + " Test\r\n"
                              "Content-Type: text/html\r\nConnection: close\r\n" + extra
                              + "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        }
    });
}

void Test_Website_ArticleLinkValidator::cleanup()
{
    disconnect(&m_server, nullptr, this, nullptr);
    m_server.close();
    const auto sockets = m_server.findChildren<QTcpSocket *>();
    for (QTcpSocket *socket : sockets) {
        delete socket;
    }
}

QString Test_Website_ArticleLinkValidator::link(const QString &path) const
{
    return QStringLiteral("[LINKFIX url=\"http://127.0.0.1:%1%2\"]study[/LINKFIX]")
        .arg(m_server.serverPort()).arg(path);
}

void Test_Website_ArticleLinkValidator::test_linkvalidator_destination_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("accepted");
    QTest::addColumn<bool>("warning");
    QTest::newRow("success") << QStringLiteral("/ok") << true << false;
    QTest::newRow("redirect") << QStringLiteral("/redirect") << true << false;
    QTest::newRow("404") << QStringLiteral("/404") << false << false;
    QTest::newRow("410") << QStringLiteral("/410") << false << false;
    QTest::newRow("redirect-to-dead") << QStringLiteral("/redirect-dead") << false << false;
    QTest::newRow("soft404") << QStringLiteral("/soft404") << false << false;
    QTest::newRow("403-not-dead") << QStringLiteral("/403") << true << true;
    QTest::newRow("429-not-dead") << QStringLiteral("/429") << true << true;
    QTest::newRow("503-not-dead") << QStringLiteral("/503") << true << true;
    QTest::newRow("captcha-200-not-verified") << QStringLiteral("/captcha") << true << true;
    QTest::newRow("empty-200-not-verified") << QStringLiteral("/empty") << true << true;
    QTest::newRow("timeout-not-dead") << QStringLiteral("/timeout") << true << true;
    QTest::newRow("redirect-loop-not-verified") << QStringLiteral("/loop") << true << true;
    QTest::newRow("bounded-body") << QStringLiteral("/large") << true << false;
}

void Test_Website_ArticleLinkValidator::test_linkvalidator_destination()
{
    QFETCH(QString, path);
    QFETCH(bool, accepted);
    QFETCH(bool, warning);
    QNetworkAccessManager network;
    ArticleLinkValidator validator(network, 500, 2000);
    const auto report = validator.check(link(path));
    QCOMPARE(report.accepted(), accepted);
    QCOMPARE(!report.warnings.isEmpty(), warning);
    QCOMPARE(report.checkedUrls, 1);
    if (!accepted) {
        QVERIFY(report.retryInstructions().contains(path));
        QVERIFY(report.retryInstructions().contains(QStringLiteral("COMPLETE updated article")));
    }
}

void Test_Website_ArticleLinkValidator::test_linkvalidator_quotes_duplicates_cache_and_relative_links()
{
    QNetworkAccessManager network;
    ArticleLinkValidator validator(network);
    const QString url = QStringLiteral("http://127.0.0.1:%1/ok").arg(m_server.serverPort());
    const QString article = link(QStringLiteral("/ok#section"))
        + QStringLiteral("[LINKTR rel='nofollow' url='%1#other']study[/LINKTR]").arg(url)
        + QStringLiteral("[LINKFIX url=%1]study[/LINKFIX]").arg(url)
        + QStringLiteral("[LINKTR url='/internal']internal[/LINKTR]"
                         "[LINKFIX url='#section']section[/LINKFIX]"
                         "[LINKFIX url='mailto:editor@example.org']email[/LINKFIX]"
                         "[LINKFIX url='tel:+33123456789']call[/LINKFIX]");
    for (int i = 0; i < 2; ++i) {
        const auto report = validator.check(article);
        QVERIFY(report.accepted());
        QVERIFY(report.warnings.isEmpty());
        QCOMPARE(report.checkedUrls, 1);
    }
    QCOMPARE(m_hits.value("/ok"), 1);
    QVERIFY(validator.check(link(QStringLiteral("/ok?different=1"))).accepted());
    QCOMPARE(m_hits.value("/ok?different=1"), 1);
}

void Test_Website_ArticleLinkValidator::test_linkvalidator_malformed_data()
{
    QTest::addColumn<QString>("article");
    QTest::newRow("whitespace") << QStringLiteral("[LINKFIX url='https://exa mple.org']x[/LINKFIX]");
    QTest::newRow("missing-host") << QStringLiteral("[LINKFIX url='https:///study']x[/LINKFIX]");
    QTest::newRow("bad-percent") << QStringLiteral("[LINKFIX url='https://example.org/%zz']x[/LINKFIX]");
    QTest::newRow("javascript") << QStringLiteral("[LINKFIX url='javascript:alert(1)']x[/LINKFIX]");
    QTest::newRow("credentials") << QStringLiteral("[LINKFIX url='https://user:pass@example.org']x[/LINKFIX]");
    QTest::newRow("empty-url") << QStringLiteral("[LINKFIX url='']x[/LINKFIX]");
    QTest::newRow("empty-label") << QStringLiteral("[LINKFIX url='https://example.org'] [/LINKFIX]");
    QTest::newRow("unclosed") << QStringLiteral("[LINKFIX url='https://example.org']x");
    QTest::newRow("mismatched") << QStringLiteral("[LINKFIX url='https://example.org']x[/LINKTR]");
    QTest::newRow("nested") << QStringLiteral("[LINKFIX url='/a'][LINKFIX url='/b']x[/LINKFIX][/LINKFIX]");
}

void Test_Website_ArticleLinkValidator::test_linkvalidator_malformed()
{
    QFETCH(QString, article);
    QNetworkAccessManager network;
    ArticleLinkValidator validator(network);
    const auto report = validator.check(article);
    QVERIFY(!report.accepted());
    QVERIFY(!report.errors.isEmpty());
    QCOMPARE(report.checkedUrls, 0);
}

void Test_Website_ArticleLinkValidator::test_linkvalidator_inconclusive_results_are_rechecked()
{
    QNetworkAccessManager network;
    ArticleLinkValidator validator(network);
    for (int i = 0; i < 2; ++i) {
        QVERIFY(!validator.check(link(QStringLiteral("/429"))).warnings.isEmpty());
        QVERIFY(!validator.check(link(QStringLiteral("/404"))).accepted());
    }
    QCOMPARE(m_hits.value("/429"), 2);
    QCOMPARE(m_hits.value("/404"), 1);
}

void Test_Website_ArticleLinkValidator::test_linkvalidator_cancellation_rejects()
{
    QNetworkAccessManager network;
    ArticleLinkValidator validator(network);
    bool stop = false;
    QTimer::singleShot(20, this, [&stop]() { stop = true; });
    const auto report = validator.check(link(QStringLiteral("/timeout")), [&stop]() { return stop; });
    QVERIFY(report.cancelled);
    QVERIFY(!report.accepted());
}

void Test_Website_ArticleLinkValidator::test_linkvalidator_total_budget_is_bounded()
{
    QNetworkAccessManager network;
    ArticleLinkValidator validator(network, 1000, 100);
    const auto report = validator.check(link(QStringLiteral("/timeout1"))
                                        + link(QStringLiteral("/timeout2")));
    QVERIFY(report.accepted());
    QCOMPARE(report.warnings.size(), 2);
    QCOMPARE(m_hits.value("/timeout2"), 0);
}

void Test_Website_ArticleLinkValidator::test_linkvalidator_known_bad_pubmed_host()
{
    TransportErrorNetwork network;
    network.failures = {QNetworkReply::HostNotFoundError, QNetworkReply::HostNotFoundError};
    ArticleLinkValidator validator(network);
    const auto report = validator.check(QStringLiteral(
        "[LINKFIX url='https://pubmed.ncbi.gov/2342128/']study[/LINKFIX]"
        "[LINKTR url='//PUBMED.NCBI.GOV.']PubMed[/LINKTR]"));
    QVERIFY(!report.accepted());
    QCOMPARE(report.errors.size(), 2);
    QVERIFY(report.warnings.isEmpty());
    QCOMPARE(network.requests, 0);
    QVERIFY(report.retryInstructions().contains(QStringLiteral("pubmed.ncbi.nlm.nih.gov")));
    QVERIFY(report.retryInstructions().contains(QStringLiteral("2342128")));
}

void Test_Website_ArticleLinkValidator::test_linkvalidator_dns_retry_data()
{
    QTest::addColumn<int>("secondFailure");
    QTest::addColumn<bool>("accepted");
    QTest::addColumn<bool>("warning");
    QTest::newRow("dns-recovered") << int(QNetworkReply::NoError) << true << false;
    QTest::newRow("dns-failed-twice") << int(QNetworkReply::HostNotFoundError) << false << false;
    QTest::newRow("dns-then-timeout") << int(QNetworkReply::TimeoutError) << true << true;
    QTest::newRow("dns-then-tls-error") << int(QNetworkReply::SslHandshakeFailedError) << true << true;
    QTest::newRow("dns-then-temporary-failure") << int(QNetworkReply::TemporaryNetworkFailureError) << true << true;
}

void Test_Website_ArticleLinkValidator::test_linkvalidator_dns_retry()
{
    QFETCH(int, secondFailure);
    QFETCH(bool, accepted);
    QFETCH(bool, warning);
    TransportErrorNetwork network;
    network.failures.append(QNetworkReply::HostNotFoundError);
    if (secondFailure != QNetworkReply::NoError) {
        network.failures.append(static_cast<QNetworkReply::NetworkError>(secondFailure));
    }
    ArticleLinkValidator validator(network);
    const auto report = validator.check(link(QStringLiteral("/ok")));
    QCOMPARE(network.requests, 2);
    QCOMPARE(report.checkedUrls, 1);
    QCOMPARE(report.accepted(), accepted);
    QCOMPARE(!report.warnings.isEmpty(), warning);
    if (!accepted) {
        QVERIFY(report.retryInstructions().contains(QStringLiteral("after retry")));
    }
}

void Test_Website_ArticleLinkValidator::test_linkvalidator_dns_rejection_is_not_cached()
{
    TransportErrorNetwork network;
    network.failures = {QNetworkReply::HostNotFoundError, QNetworkReply::HostNotFoundError};
    ArticleLinkValidator validator(network);
    QVERIFY(!validator.check(link(QStringLiteral("/ok"))).accepted());
    const auto recovered = validator.check(link(QStringLiteral("/ok")));
    QVERIFY(recovered.accepted());
    QVERIFY(recovered.warnings.isEmpty());
    QCOMPARE(network.requests, 3);
}

void Test_Website_ArticleLinkValidator::test_linkvalidator_bad_host_after_budget_exhausted()
{
    QNetworkAccessManager network;
    ArticleLinkValidator validator(network, 1000, 100);
    const auto report = validator.check(link(QStringLiteral("/timeout1")) + QStringLiteral(
        "[LINKFIX url='https://pubmed.ncbi.gov/2342128/']study[/LINKFIX]"));
    QVERIFY(!report.accepted());
    QCOMPARE(report.errors.size(), 1);
    QCOMPARE(report.warnings.size(), 1);
}

void Test_Website_ArticleLinkValidator::test_linkvalidator_dns_retry_budget_and_cancellation_data()
{
    QTest::addColumn<bool>("cancel");
    QTest::newRow("budget-exhausted-during-retry") << false;
    QTest::newRow("cancelled-during-retry") << true;
}

void Test_Website_ArticleLinkValidator::test_linkvalidator_dns_retry_budget_and_cancellation()
{
    QFETCH(bool, cancel);
    TransportErrorNetwork network;
    network.failures.append(QNetworkReply::HostNotFoundError);
    ArticleLinkValidator validator(network, 1000, cancel ? 2000 : 100);
    bool stopped = false;
    QTimer stop;
    stop.setSingleShot(true);
    connect(&stop, &QTimer::timeout, this, [&stopped]() { stopped = true; });
    if (cancel) {
        stop.start(20);
    }
    const auto report = validator.check(link(QStringLiteral("/timeout1"))
                                        + link(QStringLiteral("/timeout2")),
                                        [&stopped]() { return stopped; });
    QCOMPARE(report.cancelled, cancel);
    QCOMPARE(report.accepted(), !cancel);
    QCOMPARE(network.requests, 2); // First failure and retry; never a second URL.
    QVERIFY(!report.warnings.isEmpty());
}

void Test_Website_ArticleLinkValidator::test_linkvalidator_correct_pubmed_host_preserves_warning_policy()
{
    TransportErrorNetwork network;
    network.failures.append(QNetworkReply::TimeoutError);
    ArticleLinkValidator validator(network);
    const auto report = validator.check(QStringLiteral(
        "[LINKFIX url='https://pubmed.ncbi.nlm.nih.gov/2342128/']study[/LINKFIX]"));
    QVERIFY(report.accepted());
    QCOMPARE(report.warnings.size(), 1);
    QCOMPARE(network.requests, 1);
}

QTEST_GUILESS_MAIN(Test_Website_ArticleLinkValidator)
#include "test_website_article_link_validator.moc"
