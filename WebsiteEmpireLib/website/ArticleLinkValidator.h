#ifndef ARTICLELINKVALIDATOR_H
#define ARTICLELINKVALIDATOR_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

class QNetworkAccessManager;
class QUrl;

/**
 * Synchronous, bounded network gate for the headless article update launcher.
 * Uses a local event loop (not for interactive GUI callers). Checks every distinct
 * HTTP(S) LINKFIX/LINKTR destination, including links unchanged by a rewrite.
 * Relative links, fragments, mailto and tel are not fetched. A reachable page is
 * NOT proof that its source supports the associated claim.
 *
 * 404/410, explicit not-found HTML titles, malformed URLs and the known invalid
 * pubmed.ncbi.gov hostname reject the update. Other HostNotFound failures are
 * retried once within the article deadline; repeated failures reject this attempt
 * without being cached, so a later update can recover from a DNS outage.
 * Access blocks, timeouts, TLS errors and rate limits remain unverified warnings.
 * An unconfirmed DNS failure also warns if the retry cannot complete. The caller
 * must display warnings. Only decisive HTTP results are cached for one batch.
 */
class ArticleLinkValidator
{
public:
    struct Report {
        QStringList errors;
        QStringList warnings;
        int checkedUrls = 0;
        bool cancelled = false;
        bool accepted() const;
        QString retryInstructions() const;
    };

    explicit ArticleLinkValidator(QNetworkAccessManager &network,
                                  int requestTimeoutMs = 8000,
                                  int articleTimeoutMs = 45000);

    Report check(const QString &article,
                 const std::function<bool()> &stopRequested = {},
                 const std::function<void(const QString &)> &progress = {});

private:
    enum class Status { Reachable, Broken, Unverified, HostNotFound };
    struct Destination {
        Status status;
        QString reason;
    };
    Destination fetch(const QUrl &url, int timeoutMs,
                      const std::function<bool()> &stopRequested);

    QNetworkAccessManager &m_network;
    int m_requestTimeoutMs;
    int m_articleTimeoutMs;
    QHash<QString, Destination> m_cache;
};

#endif
