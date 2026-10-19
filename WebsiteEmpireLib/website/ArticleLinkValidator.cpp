#include "ArticleLinkValidator.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <algorithm>

bool ArticleLinkValidator::Report::accepted() const
{
    return errors.isEmpty() && !cancelled;
}

QString ArticleLinkValidator::Report::retryInstructions() const
{
    return QObject::tr(
        "The application rejected these article links:\n%1\n\n"
        "Search for and open the correct source for each failed link. Verify its "
        "identity and that it supports the surrounding claim. Correct the URL "
        "using LINKFIX/LINKTR syntax; do not invent a replacement or silently "
        "delete citations merely to pass validation. If no supporting source can "
        "be verified, qualify or remove the unsupported claim as appropriate. "
        "Return only the COMPLETE updated article, without a report.\n\n")
        .arg(errors.join(QLatin1Char('\n')));
}

ArticleLinkValidator::ArticleLinkValidator(QNetworkAccessManager &network,
                                         int requestTimeoutMs, int articleTimeoutMs)
    : m_network(network)
    , m_requestTimeoutMs(std::max(1, requestTimeoutMs))
    , m_articleTimeoutMs(std::max(1, articleTimeoutMs))
{
}

ArticleLinkValidator::Destination ArticleLinkValidator::fetch(
    const QUrl &url, int timeoutMs, const std::function<bool()> &stopRequested)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("WebsiteEmpire-LinkCheck/1.0"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    // The precise deadline below bounds the entire request, including redirects.
    // Qt's transfer timer can fire early and race that deadline.
    request.setTransferTimeout(0);
    QNetworkReply *reply = m_network.get(request);
    // Bound downloads, including PDFs and servers streaming indefinitely. A GET
    // avoids servers that incorrectly reject HEAD; only a small prefix is needed.
    constexpr qsizetype maxBytes = 128 * 1024;
    reply->setReadBufferSize(maxBytes);
    QByteArray body;
    bool timedOut = false;
    bool prefixComplete = false;
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    // Coarse timers may fire early, leaving a spurious remainder of the article
    // budget and starting another request after this one used its allowance.
    deadline.setTimerType(Qt::PreciseTimer);
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        reply->abort();
        loop.quit();
    });
    QObject::connect(reply, &QIODevice::readyRead, &loop, [&]() {
        body += reply->read(maxBytes - body.size());
        if (body.size() >= maxBytes) {
            prefixComplete = true;
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer cancellation;
    if (stopRequested) {
        QObject::connect(&cancellation, &QTimer::timeout, &loop, [&]() {
            if (stopRequested()) {
                reply->abort();
                loop.quit();
            }
        });
        cancellation.start(100);
    }
    deadline.start(timeoutMs);
    if (!reply->isFinished()) {
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    if (reply->isOpen()) {
        body += reply->read(maxBytes - body.size());
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto networkError = reply->error();
    const QString errorText = reply->errorString();
    const QString finalScheme = reply->url().scheme();
    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    reply->deleteLater();

    if (status == 404 || status == 410) {
        return {Status::Broken, QObject::tr("HTTP %1 (not found/gone)").arg(status)};
    }
    if (timedOut || (stopRequested && stopRequested())) {
        return {Status::Unverified, QObject::tr("check timed out or was cancelled")};
    }
    if (networkError == QNetworkReply::HostNotFoundError) {
        return {Status::HostNotFound, errorText};
    }
    if ((networkError != QNetworkReply::NoError && !prefixComplete)
        || status < 200 || status >= 300
        || (finalScheme != QStringLiteral("http") && finalScheme != QStringLiteral("https"))) {
        return {Status::Unverified, QObject::tr("HTTP %1; %2").arg(status).arg(errorText)};
    }
    // Inspect titles only: finding "404" in normal article prose would reject
    // legitimate medical studies. Do not infer source relevance from a title.
    static const QRegularExpression titleRe(
        QStringLiteral("<title\\b[^>]*>(.*?)</title>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const auto titleMatch = titleRe.match(QString::fromUtf8(body));
    const QString title = titleMatch.captured(1).simplified();
    static const QRegularExpression missingTitle(
        QStringLiteral("^(?:(?:404|410)(?:\\s+error)?[\\s:|–—-]*)?"
                       "(?:page not found|not found|page cannot be found|page does not exist)"
                       "(?:$|\\s*[-|:–—].*)"), QRegularExpression::CaseInsensitiveOption);
    if (missingTitle.match(title).hasMatch()) {
        return {Status::Broken, QObject::tr("not-found page: %1").arg(title)};
    }
    static const QRegularExpression blockedTitle(
        QStringLiteral("checking your browser|just a moment|access denied|"
                       "human verification|attention required|captcha|verify you are human"),
        QRegularExpression::CaseInsensitiveOption);
    if (blockedTitle.match(title).hasMatch()) {
        return {Status::Unverified, QObject::tr("automated access blocked: %1").arg(title)};
    }
    if (body.isEmpty() || (contentType.contains(QStringLiteral("text/html")) && title.isEmpty())) {
        return {Status::Unverified, QObject::tr("empty response or HTML without a verifiable title")};
    }
    return {Status::Reachable, {}};
}

ArticleLinkValidator::Report ArticleLinkValidator::check(
    const QString &article, const std::function<bool()> &stopRequested,
    const std::function<void(const QString &)> &progress)
{
    Report report;
    QElapsedTimer budget;
    budget.start();
    static const QRegularExpression linkRe(
        QStringLiteral("\\[(LINKFIX|LINKTR)\\b([^\\]]*)\\]([\\s\\S]*?)\\[/\\1\\]"));
    static const QRegularExpression urlRe(
        QStringLiteral(R"re((?:^|\s)url=(?:"([^"]*)"|'([^']*)'|([^\s]+)))re"));
    static const QRegularExpression whitespace(QStringLiteral("\\s"));
    static const QRegularExpression linkToken(QStringLiteral("\\[/?LINK(?:FIX|TR)\\b"));
    const auto completeLinks = linkRe.globalMatch(article);
    auto completeCount = completeLinks;
    int pairs = 0;
    while (completeCount.hasNext()) {
        const auto match = completeCount.next();
        ++pairs;
        if (linkToken.match(match.captured(3)).hasMatch()) {
            report.errors.append(QObject::tr("Nested link shortcodes are not valid."));
        }
    }
    int tokens = 0;
    auto tokenMatches = linkToken.globalMatch(article);
    while (tokenMatches.hasNext()) {
        tokenMatches.next();
        ++tokens;
    }
    if (tokens != 2 * pairs) {
        report.errors.append(QObject::tr("Unclosed or mismatched LINKFIX/LINKTR shortcode."));
    }
    if (!report.errors.isEmpty()) {
        return report;
    }
    QSet<QString> seen;
    auto matches = completeLinks;
    while (matches.hasNext()) {
        if (stopRequested && stopRequested()) {
            report.cancelled = true;
            break;
        }
        const auto match = matches.next();
        const auto urlMatch = urlRe.match(match.captured(2));
        QString rawUrl;
        for (int group = 1; group <= 3; ++group) {
            if (!urlMatch.captured(group).isNull()) {
                rawUrl = urlMatch.captured(group);
                break;
            }
        }
        QUrl url(rawUrl, QUrl::StrictMode);
        if (rawUrl.isEmpty() || whitespace.match(rawUrl).hasMatch()
            || !url.isValid() || match.captured(3).trimmed().isEmpty()) {
            report.errors.append(QObject::tr("Malformed link URL or empty label: %1").arg(rawUrl));
            continue;
        }
        if (url.isRelative() && url.host().isEmpty()) {
            continue; // Internal links require site routing information, not HTTP.
        }
        if (url.scheme().isEmpty() && !url.host().isEmpty()) {
            url.setScheme(QStringLiteral("https")); // Protocol-relative destination.
        }
        const QString scheme = url.scheme();
        if (scheme == QStringLiteral("mailto") || scheme == QStringLiteral("tel")) {
            continue;
        }
        if ((scheme != QStringLiteral("https") && scheme != QStringLiteral("http"))
            || url.host().isEmpty() || !url.userInfo().isEmpty()) {
            report.errors.append(QObject::tr("Invalid article link destination: %1").arg(rawUrl));
            continue;
        }
        url.setFragment(QString()); // Same document is fetched only once.
        const QString key = url.toString(QUrl::FullyEncoded);
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        QString host = url.host().toLower();
        if (host.endsWith(QLatin1Char('.'))) {
            host.chop(1); // DNS permits an absolute hostname with a trailing dot.
        }
        if (host == QStringLiteral("pubmed.ncbi.gov")) {
            ++report.checkedUrls;
            report.errors.append(QObject::tr(
                "%1 — incorrect PubMed hostname: use pubmed.ncbi.nlm.nih.gov, "
                "then open the source and verify the study identifier and claim.").arg(rawUrl));
            continue; // Deterministic URL error, even when the network budget is spent.
        }
        if (progress) {
            progress(rawUrl);
        }
        const int remaining = m_articleTimeoutMs - static_cast<int>(budget.elapsed());
        Destination result{Status::Unverified, QObject::tr("article link-check time budget exhausted")};
        const auto cached = m_cache.constFind(key);
        if (cached != m_cache.cend()) {
            result = cached.value();
        } else if (remaining > 0) {
            result = fetch(url, std::min(remaining, m_requestTimeoutMs), stopRequested);
            const bool dnsFailed = result.status == Status::HostNotFound;
            if (dnsFailed) {
                const int retryRemaining = m_articleTimeoutMs - static_cast<int>(budget.elapsed());
                if (retryRemaining > 0 && !(stopRequested && stopRequested())) {
                    result = fetch(url, std::min(retryRemaining, m_requestTimeoutMs), stopRequested);
                    if (result.status == Status::HostNotFound) {
                        result = {Status::Broken, QObject::tr(
                            "hostname could not be resolved after retry; verify the URL "
                            "and DNS connectivity before accepting this update (%1)").arg(result.reason)};
                    }
                } else {
                    result = {Status::Unverified, QObject::tr(
                        "hostname lookup failed; retry could not complete within the "
                        "article deadline or was cancelled (%1)").arg(result.reason)};
                }
            }
            // DNS failures can be temporary even when repeated. Do not carry a
            // DNS-based rejection into the next CLI attempt or article.
            if (result.status == Status::Reachable || (result.status == Status::Broken && !dnsFailed)) {
                m_cache.insert(key, result);
            }
        }
        ++report.checkedUrls;
        if (result.status == Status::Broken) {
            report.errors.append(QObject::tr("%1 — %2").arg(rawUrl, result.reason));
        } else if (result.status == Status::Unverified) {
            report.warnings.append(QObject::tr("%1 — %2").arg(rawUrl, result.reason));
        }
    }
    if (stopRequested && stopRequested()) {
        report.cancelled = true;
    }
    return report;
}
