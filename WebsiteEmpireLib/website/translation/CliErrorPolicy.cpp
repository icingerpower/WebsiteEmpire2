#include "CliErrorPolicy.h"

#include <QTextStream>

#include <poll.h>
#include <unistd.h>

namespace {

// Nobody typed anything on quota/auth errors: retry automatically after this
// long, rather than blocking the run forever. Sometimes the quota resets on
// its own; if it hasn't, the run just comes back to the same pause with the
// same question.
constexpr int kAutoRetryAfterMs = 10 * 60 * 1000;

// Blocks up to timeoutMs waiting for a line to become available on stdin.
// Returns false on timeout (no line ready), true if one is ready to read.
bool waitForStdinLine(int timeoutMs)
{
    pollfd pfd{STDIN_FILENO, POLLIN, 0};
    return poll(&pfd, 1, timeoutMs) > 0;
}

} // namespace

CliErrorAction defaultCliErrorCallback(const AbstractCli &/*cli*/,
                                       CliErrorKind        /*kind*/,
                                       const QString       &/*rawError*/)
{
    return CliErrorAction::Skip;
}

CliErrorAction interactivePauseCliErrorCallback(const AbstractCli &cli,
                                                CliErrorKind        kind,
                                                const QString       &rawError)
{
    if (kind != CliErrorKind::QuotaExceeded && kind != CliErrorKind::AuthRequired) {
        return CliErrorAction::Skip;
    }

    const QString &reason = kind == CliErrorKind::AuthRequired
        ? QStringLiteral("login session expired")
        : QStringLiteral("quota/rate limit reached");

    QTextStream qout(stdout);
    QTextStream qin(stdin);
    qout << "\n[" << cli.getName() << "] " << reason << ":\n" << rawError
         << "\n\nFix it, then press Enter to retry this job — "
            "or type 's' to skip it, 'a' to abort the run "
            "(auto-retries in 10 minutes if you don't answer): " << Qt::flush;

    if (!waitForStdinLine(kAutoRetryAfterMs)) {
        qout << "\n[no response after 10 minutes — retrying automatically]\n" << Qt::flush;
        return CliErrorAction::Retry;
    }

    const QString answer = qin.readLine().trimmed().toLower();
    if (answer == QStringLiteral("a")) {
        return CliErrorAction::Abort;
    }
    if (answer == QStringLiteral("s")) {
        return CliErrorAction::Skip;
    }
    return CliErrorAction::Retry;
}
