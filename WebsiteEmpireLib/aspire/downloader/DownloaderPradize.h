#ifndef DOWNLOADERPRADIZE_H
#define DOWNLOADERPRADIZE_H

#include "AbstractDownloader.h"

class QNetworkAccessManager;

// Crawler for product pages on pradize.com (CommerceHQ platform).
//
// Product discovery uses the CommerceHQ FES (Frontend E-Shop) API:
//   • Session:   GET /api/v1/fes/token  → sets _rclientSessionId cookie
//                Required for ALL requests (HTML + API) — without it the
//                server returns the bare Angular SPA shell (11 KB).
//                Acquired once before the very first request of any type.
//   • Products:  GET /api/v1/fes/products?collection=<slug>&page=1&limit=100
//                → JSON {items:[{seo_url,...}], _links:{next:{href:...}}}
//
// Crawl flow:
//   1. Seed URLs are HTML collection pages.
//   2. getUrlsToParse(HTML) extracts /collection/<slug> hrefs AND generates
//      the corresponding API products URLs.
//   3. getUrlsToParse(JSON) from a products API response extracts product HTML
//      URLs (https://pradize.com/product/<seo_url>) + _links.next pagination.
//   4. getAttributeValues() parses product HTML via the builderApp-state script.
//
// Product HTML data (builderApp-state script):
//   JSON maps opaque hash keys to objects whose "body" field holds the product:
//   • "title"          — product name
//   • "type"           — product category (e.g. "Dress Long", "Shoes")
//   • "default_price"  — sale price
//   • "seo_url"        — URL slug, used to identify the current product
//   • "images"         — array of {id, path} objects (main image set)
//   • "textareas"      — array of {name, text} objects; first entry is the
//                        HTML description, stripped to plain text
//   • "options"        — array of option objects with "title" and "values":
//       - "Size"  → values like "US-2 | UK/AU-6 | EU/DE-32 | FR/ES-34"
//                   or "US-3 | UK/AU-1 | EU-34" (shoes: EU == FR)
//       - "Color" → values like "Dark Brown", "Beige", "Black"
//   • "variants"       — per-variant {images, price, ...}; used as fallback
//                        image source when the top-level "images" array is empty
//
// getAttributeValues() returns a PageAttributesProductFashion attribute map
// plus a special PRADIZE_IMAGE_URLS_KEY holding a semicolon-separated list
// of image URLs (up to MAX_IMAGES). Callers are responsible for fetching
// those URLs and supplying the images to DownloadedPagesTable::recordPage().
//
// Color detection:
//   1. Use the first value of the "Color" option when present.
//   2. Otherwise search the product title and then description for known
//      colour keywords (compound multi-word colours checked before single
//      words so "Blush Pink" is preferred over just "Pink").
class DownloaderPradize : public AbstractDownloader
{
    Q_OBJECT

public:
    // Maximum number of image URLs returned in PRADIZE_IMAGE_URLS_KEY.
    static const int MAX_IMAGES = 10;

    explicit DownloaderPradize(const QDir &workingDir = QDir{},
                               PageParsedCallback onPageParsed = {},
                               QObject *parent = nullptr);

    AbstractDownloader *createInstance(const QDir &workingDir) const override;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    AbstractPageAttributes *createPageAttributes() const override;

    // Returns true — this downloader processes targeted URL lists.
    bool supportsFileUrlDownload() const override;

    // 2-second inter-request delay to avoid triggering pradize.com's rate limiter.
    int requestDelayMs() const override { return 2000; }

    // A valid pradize product/collection page is ~200 KB.  Pages under 10 KB
    // are rate-limit error responses; re-enqueue them instead of marking visited.
    // Exception: the sentinel PRADIZE_404_SENTINEL is returned by doFetch() when
    // the server responds with HTTP 404; isFetchSuccessful() treats it as success
    // so the URL is marked visited and never re-enqueued.
    bool isFetchSuccessful(const QString &url, const QString &content) const override;

    // Seeds with a set of known pradize.com collection page URLs, from which
    // getUrlsToParseenregistré() discovers product and further collection URLs.
    QStringList getSeedUrls() const override;

    // Returns PRADIZE_IMAGE_URLS_KEY so WidgetDownloader knows which text
    // attribute carries the semicolon-separated image URLs to fetch.
    QString getImageUrlAttributeKey() const override;

    // For HTML content: extracts /product/<slug> and /collection/<slug> hrefs
    // (encoded with "&q;" entities or as plain hrefs); for each collection slug
    // found also enqueues the FES API products URL so the crawl discovers all
    // products dynamically (CommerceHQ loads products via the API, not HTML).
    // For JSON content (FES products API response): extracts product HTML URLs
    // from items[].seo_url and the next-page URL from _links.next.href.
    QStringList getUrlsToParse(const QString &content) const override;

    // Returns PageAttributesProductFashion attributes when the URL is a
    // product page (/product/<slug>) and the product body is found in the
    // builderApp-state JSON. Returns an empty hash otherwise.
    QHash<QString, QString> getAttributeValues(const QString &url, const QString &content) const override;

    // Supplies the list of known category names so that getAttributeValues()
    // can resolve the raw product type to an existing database category.
    void setKnownCategories(const QStringList &categories);

protected:
    QFuture<QString> fetchUrl(const QString &url) override;

private:
    // 4-tier category matching: exact → substring → keyword overlap → first.
    QString matchCategory(const QString &rawCategory) const;

    // Raw network fetch (no session acquisition).
    QFuture<QString> doFetch(const QString &url);

    // Called after every fetch to track degraded responses.
    // After 5 total degraded responses since the last session reset (successes
    // between failures do NOT reset the counter — the old "consecutive" approach
    // failed when successes and failures alternated, keeping the counter below 5
    // indefinitely despite a high overall failure rate):
    //   1. Resets the FES session cookie so a fresh _rclientSessionId is acquired.
    //   2. Schedules a 30-second backoff (stored in m_backoffUntilMs).
    void trackFetchResult(const QString &url, const QString &content);

    QNetworkAccessManager *m_nam = nullptr;
    QStringList m_knownCategories;
    bool m_sessionAcquired = false;
    // Total degraded responses since the last session reset.
    // NOT reset on success — see trackFetchResult().
    int m_degradedCount = 0;
    // Epoch-ms timestamp before which the next network fetch must not start.
    // Set by trackFetchResult() when sustained rate-limiting is detected.
    // fetchUrl() waits out any remaining backoff via QTimer before proceeding.
    qint64 m_backoffUntilMs = 0;
};

// Key used to pass the semicolon-separated list of image URLs out of
// getAttributeValues() without polluting the schema.
inline const QString PRADIZE_IMAGE_URLS_KEY = "__pradize_image_urls__";

// Sentinel returned by doFetch() when the server replies with HTTP 404.
// isFetchSuccessful() recognises it so the URL is marked visited (not
// re-enqueued), and getAttributeValues() naturally returns an empty map.
inline const QString PRADIZE_404_SENTINEL = "__pradize_404__";

#endif // DOWNLOADERPRADIZE_H

