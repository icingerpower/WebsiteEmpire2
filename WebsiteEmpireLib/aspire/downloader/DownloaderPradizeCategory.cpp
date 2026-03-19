#include "DownloaderPradizeCategory.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include "aspire/attributes/PageAttributesProductCategory.h"

DECLARE_DOWNLOADER(DownloaderPradizeCategory)

static const QString PRADIZE_DOMAIN = QStringLiteral("pradize.com");

DownloaderPradizeCategory::DownloaderPradizeCategory(const QDir &workingDir,
                                                     PageParsedCallback onPageParsed,
                                                     QObject *parent)
    : AbstractDownloader(workingDir, std::move(onPageParsed), parent)
{
}

AbstractDownloader *DownloaderPradizeCategory::createInstance(const QDir &workingDir) const
{
    return new DownloaderPradizeCategory(workingDir);
}

QString DownloaderPradizeCategory::getId() const
{
    return QStringLiteral("pradize_category");
}

QString DownloaderPradizeCategory::getName() const
{
    return tr("Pradize.com — Categories");
}

QString DownloaderPradizeCategory::getDescription() const
{
    return tr("Crawler for collection/category pages on pradize.com.");
}

AbstractPageAttributes *DownloaderPradizeCategory::createPageAttributes() const
{
    return new PageAttributesProductCategory;
}

bool DownloaderPradizeCategory::supportsFileUrlDownload() const
{
    return true;
}

bool DownloaderPradizeCategory::isFetchSuccessful(const QString &url, const QString &content) const
{
    if (url.contains(QLatin1String("/api/v1/fes/"))) {
        return !content.isEmpty();
    }
    // The rate-limit error page from pradize.com is ~11 KB; real pages are 100+ KB.
    return content.length() >= 20000;
}

QStringList DownloaderPradizeCategory::getSeedUrls() const
{
    return {
        // FES collections API — discovers ALL collections with full pagination.
        QStringLiteral("https://pradize.com/api/v1/fes/collections?limit=100"),
        // HTML seed pages kept as fallback / cross-check.
        QStringLiteral("https://pradize.com/collection/ethereal-dresses-fancy-flowy-gowns"),
        QStringLiteral("https://pradize.com/collection/classy-evening-maxi-dresses-long-formal"),
        QStringLiteral("https://pradize.com/collection/glitter-sequins-shimmery-sparkle-evening-dresses"),
        QStringLiteral("https://pradize.com/collection/midi-dresses"),
        QStringLiteral("https://pradize.com/collection/mini-dresses"),
        QStringLiteral("https://pradize.com/collection/summer-dresses"),
        QStringLiteral("https://pradize.com/collection/workwear-dresses"),
        QStringLiteral("https://pradize.com/collection/corset-dresses"),
        QStringLiteral("https://pradize.com/collection/floral-dresses"),
        QStringLiteral("https://pradize.com/collection/dresses-puffed-sleeves"),
        QStringLiteral("https://pradize.com/collection/dresses-bodycon"),
        QStringLiteral("https://pradize.com/collection/heels-pumps-stiletto-block"),
        QStringLiteral("https://pradize.com/collection/ankle-boots-women"),
        QStringLiteral("https://pradize.com/collection/knee-high-heeled-boots"),
        QStringLiteral("https://pradize.com/collection/block-heel-sandals"),
        QStringLiteral("https://pradize.com/collection/stiletto-high-heel-sandals"),
        QStringLiteral("https://pradize.com/collection/platform-heels-sandals"),
        QStringLiteral("https://pradize.com/collection/sexy-sleepwear-nightgowns-pajamas-nighties"),
        QStringLiteral("https://pradize.com/collection/all-jumpsuits"),
        QStringLiteral("https://pradize.com/collection/ladies-tops-and-blouses"),
        QStringLiteral("https://pradize.com/collection/skirts"),
        QStringLiteral("https://pradize.com/collection/formal-short-dresses-tight-classy"),
        QStringLiteral("https://pradize.com/collection/black-elegant-long-dresses"),
        QStringLiteral("https://pradize.com/collection/party-night-club-dresses-sexy"),
    };
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Decodes HTML entity encoding used by the CommerceHQ platform inside
// <script id="builderApp-state"> tags.
static QString decodeCommerceHqEntities(const QString &s)
{
    QString result = s;
    result.replace(QLatin1String("&q;"),   QLatin1String("\""));
    result.replace(QLatin1String("&l;"),   QLatin1String("<"));
    result.replace(QLatin1String("&g;"),   QLatin1String(">"));
    result.replace(QLatin1String("&s;"),   QLatin1String("'"));
    result.replace(QLatin1String("&a;"),   QLatin1String("&"));
    result.replace(QLatin1String("&amp;"), QLatin1String("&"));
    result.replace(QLatin1String("&lt;"),  QLatin1String("<"));
    result.replace(QLatin1String("&gt;"),  QLatin1String(">"));
    result.replace(QLatin1String("&#039;"),QLatin1String("'"));
    result.replace(QLatin1String("&quot;"),QLatin1String("\""));
    return result;
}

// Extracts and decodes the builderApp-state JSON from a page.
// Returns a null QJsonObject if the script is absent or unparseable.
static QJsonObject extractStateJson(const QString &content)
{
    static const QRegularExpression stateRe(
        QStringLiteral(R"(<script\s[^>]*id\s*=\s*"builderApp-state"[^>]*>([\s\S]*?)</script>)"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch m = stateRe.match(content);
    if (!m.hasMatch()) {
        return {};
    }

    const QString decoded = decodeCommerceHqEntities(m.captured(1));
    const QJsonDocument doc = QJsonDocument::fromJson(decoded.toUtf8());
    return doc.object();
}

// ---------------------------------------------------------------------------
// URL extraction
// ---------------------------------------------------------------------------

// Parses a CommerceHQ FES collections API JSON response and returns:
//   • One https://pradize.com/collection/<seo_url> URL per item
//   • The _links.next.href pagination URL when present
static QStringList getUrlsFromCollectionsApiJson(const QString &content)
{
    QStringList urls;
    QSet<QString> seen;

    auto add = [&](const QString &url) {
        if (!seen.contains(url)) {
            seen.insert(url);
            urls << url;
        }
    };

    const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8());
    if (!doc.isObject()) {
        return {};
    }
    const QJsonObject obj = doc.object();

    // Extract collection HTML page URLs from items[].seo_url.
    const QJsonArray items = obj[QStringLiteral("items")].toArray();
    for (const QJsonValue &item : items) {
        if (!item.isObject()) {
            continue;
        }
        const QString seoUrl = item.toObject()[QStringLiteral("seo_url")].toString().trimmed();
        if (!seoUrl.isEmpty()) {
            add(QStringLiteral("https://pradize.com/collection/") + seoUrl);
        }
    }

    // Add next-page API URL for pagination.
    const QJsonObject links   = obj[QStringLiteral("_links")].toObject();
    const QJsonObject nextObj = links[QStringLiteral("next")].toObject();
    const QString nextHref    = nextObj[QStringLiteral("href")].toString().trimmed();
    if (!nextHref.isEmpty()) {
        add(nextHref);
    }

    return urls;
}

QStringList DownloaderPradizeCategory::getUrlsToParse(const QString &content) const
{
    // FES API JSON response: extract collection HTML URLs + pagination.
    if (content.trimmed().startsWith(QLatin1Char('{'))) {
        return getUrlsFromCollectionsApiJson(content);
    }

    // HTML page: extract /collection/<slug> hrefs.
    QStringList urls;
    QSet<QString> seen;

    auto add = [&](const QString &url) {
        if (!seen.contains(url)) {
            seen.insert(url);
            urls << url;
        }
    };

    // Decode &q; entities so we can find href patterns in the state JSON.
    const QString decoded = decodeCommerceHqEntities(content);

    // Relative /collection/<slug> paths
    static const QRegularExpression collRe(
        QStringLiteral(R"(/collection/([a-z0-9][a-z0-9-]*)(?=[^a-z0-9-]|$))"));
    auto it = collRe.globalMatch(decoded);
    while (it.hasNext()) {
        add(QStringLiteral("https://") + PRADIZE_DOMAIN + it.next().captured(0));
    }

    // Full https://pradize.com/collection/<slug> URLs already in content
    static const QRegularExpression fullCollRe(
        QStringLiteral(R"(https?://(?:www\.)?pradize\.com/collection/([a-z0-9][a-z0-9-]*))"));
    auto it2 = fullCollRe.globalMatch(decoded);
    while (it2.hasNext()) {
        const QRegularExpressionMatch match = it2.next();
        add(QStringLiteral("https://pradize.com/collection/") + match.captured(1));
    }

    return urls;
}

// ---------------------------------------------------------------------------
// Attribute extraction
// ---------------------------------------------------------------------------

QHash<QString, QString> DownloaderPradizeCategory::getAttributeValues(
    const QString &url, const QString &content) const
{
    // Only process collection pages.
    if (!url.contains(QLatin1String("/collection/"))) {
        return {};
    }

    // Extract slug from URL: /collection/<slug>
    static const QRegularExpression slugRe(QStringLiteral(R"(/collection/([^/?#]+))"));
    const QRegularExpressionMatch slugMatch = slugRe.match(url);
    if (!slugMatch.hasMatch()) {
        return {};
    }
    const QString urlSlug = slugMatch.captured(1);

    const QJsonObject stateObj = extractStateJson(content);
    if (stateObj.isEmpty()) {
        return {};
    }

    // Find the collection body whose seo_url matches the URL slug.
    // Collection bodies have "type": 0 to distinguish them from product bodies.
    for (const QJsonValue &val : stateObj) {
        if (!val.isObject()) {
            continue;
        }
        const QJsonObject entry = val.toObject();
        const QJsonValue bodyVal = entry[QStringLiteral("body")];
        if (!bodyVal.isObject()) {
            continue;
        }
        const QJsonObject body = bodyVal.toObject();

        // Must be a collection (type == 0) with matching seo_url.
        if (body[QStringLiteral("seo_url")].toString() != urlSlug) {
            continue;
        }
        if (body[QStringLiteral("type")].toInt(-1) != 0) {
            continue;
        }

        const QString title = body[QStringLiteral("title")].toString().trimmed();
        if (title.isEmpty()) {
            continue;
        }

        QString description = body[QStringLiteral("description")].toString().trimmed();
        if (description.length() < 10) {
            description = tr("Discover our %1 collection.").arg(title);
        }

        QHash<QString, QString> result;
        result[PageAttributesProductCategory::ID_URL]              = url;
        result[PageAttributesProductCategory::ID_PRODUCT_CATEGORY] = title;
        result[PageAttributesProductCategory::ID_DESCRIPTION]      = description;
        return result;
    }

    return {};
}

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------

QFuture<QString> DownloaderPradizeCategory::doFetch(const QString &url)
{
    auto promise = QSharedPointer<QPromise<QString>>::create();
    promise->start();

    QNetworkRequest request{QUrl{url}};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (compatible; WebsiteEmpire/1.0)"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, promise]() {
        promise->addResult(QString::fromUtf8(reply->readAll()));
        promise->finish();
        reply->deleteLater();
    });

    return promise->future();
}

QFuture<QString> DownloaderPradizeCategory::fetchUrl(const QString &url)
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
    }

    // The _rclientSessionId cookie (set by the FES token endpoint) is required
    // for ALL requests to pradize.com — without it the server returns the bare
    // Angular SPA shell (11 KB) instead of the SSR page.
    // Acquire it lazily before the very first request of any type.
    if (!m_sessionAcquired) {
        m_sessionAcquired = true;
        const QString tokenUrl =
            QStringLiteral("https://pradize.com/api/v1/fes/token");
        return doFetch(tokenUrl).then(this, [this, url](const QString &) -> QFuture<QString> {
            return doFetch(url);
        }).unwrap();
    }

    return doFetch(url);
}
