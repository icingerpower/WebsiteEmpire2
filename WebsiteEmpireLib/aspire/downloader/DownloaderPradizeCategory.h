#ifndef DOWNLOADERPRADIZECATEGORY_H
#define DOWNLOADERPRADIZECATEGORY_H

#include "AbstractDownloader.h"

class QNetworkAccessManager;

// Crawler for collection/category pages on pradize.com (CommerceHQ platform).
//
// Category discovery uses the CommerceHQ FES (Frontend E-Shop) collections API:
//   • Session:     GET /api/v1/fes/token  → sets _rclientSessionId cookie
//   • Collections: GET /api/v1/fes/collections?limit=100&page=N
//                  → JSON {items:[{seo_url,...}], _links:{next:{href:...}}}
//
// The API seed URL is included in getSeedUrls() alongside HTML collection pages.
// getUrlsToParse() detects JSON responses and extracts collection HTML URLs from
// items[].seo_url + the next-page URL from _links.next.href for full pagination.
//
// Collection HTML pages embed their data in a <script id="builderApp-state"
// type="application/json"> tag where all double-quotes are HTML-entity-encoded
// as "&q;". The collection body carries:
//   • "title"       — human-readable collection name (e.g. "Ankle Boots")
//   • "description" — short collection description
//   • "type"        — 0 for collections (used to distinguish from products)
//   • "seo_url"     — URL slug (e.g. "ankle-boots-women")
//
// getAttributeValues() accepts URLs of the form /collection/<slug> and
// extracts PageAttributesProductCategory attributes.  All other URLs
// (product pages, API URLs, etc.) are silently rejected with an empty hash.
class DownloaderPradizeCategory : public AbstractDownloader
{
    Q_OBJECT

public:
    explicit DownloaderPradizeCategory(const QDir &workingDir = QDir{},
                                       PageParsedCallback onPageParsed = {},
                                       QObject *parent = nullptr);

    AbstractDownloader *createInstance(const QDir &workingDir) const override;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    AbstractPageAttributes *createPageAttributes() const override;

    // Returns true — this downloader can process targeted URL lists supplied
    // by the user (e.g. a curated list of collection page URLs).
    bool supportsFileUrlDownload() const override;

    // 2-second inter-request delay to avoid triggering pradize.com's rate limiter.
    int requestDelayMs() const override { return 2000; }

    // Re-enqueue pages shorter than 10 KB — these are rate-limit error responses.
    bool isFetchSuccessful(const QString &url, const QString &content) const override;

    // Seeds with a set of known pradize.com collection page URLs.
    QStringList getSeedUrls() const override;

    // For JSON content (FES collections API response): extracts collection HTML
    // URLs from items[].seo_url and the next-page URL from _links.next.href.
    // For HTML content: extracts /collection/<slug> hrefs so the crawl can
    // discover further collection pages.
    QStringList getUrlsToParse(const QString &content) const override;

    // Returns PageAttributesProductCategory attributes when the URL is a
    // collection page (/collection/<slug>) and the collection data is found
    // in the builderApp-state JSON. Returns an empty hash otherwise.
    QHash<QString, QString> getAttributeValues(const QString &url, const QString &content) const override;

protected:
    QFuture<QString> fetchUrl(const QString &url) override;

private:
    // Raw network fetch (no session acquisition).
    QFuture<QString> doFetch(const QString &url);

    QNetworkAccessManager *m_nam = nullptr;
    bool m_sessionAcquired = false;
};

#endif // DOWNLOADERPRADIZECATEGORY_H
