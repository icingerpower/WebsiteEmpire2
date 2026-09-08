#include <QtTest>

#include "aicli/AbstractCli.h"
#include "aicli/CliCodex.h"

// classifyError() is what LauncherGeneration's raster-image generate/review/
// retry loop relies on to distinguish a transient CLI usage-limit/auth error
// (pause, do not count against the retry budget) from an ordinary content
// failure (retry).  Codex had no classification at all before this session —
// every error fell through to Other, so a quota hit would have silently
// burned all retry attempts and permanently failed the image instead of
// pausing for the next run.
class Test_Website_CliCodex : public QObject
{
    Q_OBJECT

private slots:
    void test_codex_classify_refresh_token_expired_is_auth_required();
    void test_codex_classify_sign_in_again_is_auth_required();
    void test_codex_classify_rate_limit_is_quota_exceeded();
    void test_codex_classify_429_is_quota_exceeded();
    void test_codex_classify_unrelated_error_is_other();
};

void Test_Website_CliCodex::test_codex_classify_refresh_token_expired_is_auth_required()
{
    CliCodex cli;
    const QString err = QStringLiteral(
        "Failed to refresh token: 401 Unauthorized: "
        "{\"error\":{\"message\":\"...\",\"code\":\"refresh_token_expired\"}}");

    QCOMPARE(cli.classifyError(err), CliErrorKind::AuthRequired);
}

void Test_Website_CliCodex::test_codex_classify_sign_in_again_is_auth_required()
{
    CliCodex cli;
    const QString err = QStringLiteral(
        "Your access token could not be refreshed. Please log out and sign in again.");

    QCOMPARE(cli.classifyError(err), CliErrorKind::AuthRequired);
}

void Test_Website_CliCodex::test_codex_classify_rate_limit_is_quota_exceeded()
{
    CliCodex cli;
    QCOMPARE(cli.classifyError(QStringLiteral("Error: rate limit exceeded, try again later")),
             CliErrorKind::QuotaExceeded);
}

void Test_Website_CliCodex::test_codex_classify_429_is_quota_exceeded()
{
    CliCodex cli;
    QCOMPARE(cli.classifyError(QStringLiteral("HTTP 429 Too Many Requests")),
             CliErrorKind::QuotaExceeded);
}

void Test_Website_CliCodex::test_codex_classify_unrelated_error_is_other()
{
    CliCodex cli;
    QCOMPARE(cli.classifyError(QStringLiteral("exit code 1")), CliErrorKind::Other);
}

QTEST_MAIN(Test_Website_CliCodex)
#include "test_cli_codex.moc"
