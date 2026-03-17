#ifndef DOWNLOADERVOGELVOERKOPEN_H
#define DOWNLOADERVOGELVOERKOPEN_H

#include "AbstractDownloader.h"

class QNetworkAccessManager;

// Crawler for vogelvoerkopen.nl — a Dutch pet-food WordPress/WooCommerce shop.
//
// Product pages are identified by a JSON-LD <script type="application/ld+json">
// block whose "@type" is "Product".  All other pages (shop, categories, home)
// yield new links but no attribute values.
//
// getAttributeValues() returns the full set of PageAttributesProductPetFood
// text attributes plus a special "__image_url__" key (not part of the schema)
// containing the first product image URL.  Callers are responsible for fetching
// that URL and supplying the image to DownloadedPagesTable::recordPage().
//
// Weight and prices are extracted from WooCommerce data-product_variations JSON
// when present (variable products); for simple products the weight is inferred
// from the product name using unit-aware regexes and the price from offers.price.
class DownloaderVogelvoerkopen : public AbstractDownloader
{
    Q_OBJECT

public:
    explicit DownloaderVogelvoerkopen(const QDir &workingDir = QDir{},
                                      PageParsedCallback onPageParsed = {},
                                      QObject *parent = nullptr);

    AbstractDownloader *createInstance(const QDir &workingDir) const override;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    AbstractPageAttributes *createPageAttributes() const override;

    // Seed URL: the WooCommerce product sitemap lists every product URL directly.
    QStringList getSeedUrls() const override;

    // Returns VOGELVOERKOPEN_IMAGE_URL_KEY so WidgetDownloader knows which text
    // attribute carries the image URL to fetch.
    QString getImageUrlAttributeKey() const override;

    // Extracts href links on the vogelvoerkopen.nl domain.
    // Also handles XML sitemap responses: parses <loc> elements.
    QStringList getUrlsToParse(const QString &content) const override;

    // Returns the PageAttributesProductPetFood attribute map when the page is a
    // product page, otherwise an empty hash.
    // Extra key "__image_url__" holds the first product image URL.
    // When knownCategories have been set via setKnownCategories(), the raw
    // breadcrumb category is matched against them before insertion.
    QHash<QString, QString> getAttributeValues(const QString &content) const override;

    // Supplies the list of known category names (from DownloaderVogelvoerkopenCategory)
    // so that getAttributeValues() can resolve the product's raw breadcrumb category
    // to an existing database category instead of storing an ad-hoc string.
    void setKnownCategories(const QStringList &categories);

protected:
    QFuture<QString> fetchUrl(const QString &url) override;

private:
    // Finds the best match for rawCategory in m_knownCategories:
    //   1. exact (case-insensitive)
    //   2. substring containment
    //   3. keyword overlap scoring
    //   4. default: first known category
    // Returns rawCategory unchanged when m_knownCategories is empty.
    QString matchCategory(const QString &rawCategory) const;

    // Lazily initialised in fetchUrl() so that the registry instance created
    // by DECLARE_DOWNLOADER (before QCoreApplication exists) never constructs
    // a QNetworkAccessManager, avoiding "invalid nullptr" connect() warnings.
    QNetworkAccessManager *m_nam = nullptr;

    QStringList m_knownCategories;
};

// Key used to pass image URLs out of getAttributeValues() without polluting the schema.
inline const QString VOGELVOERKOPEN_IMAGE_URL_KEY = "__image_url__";

#endif // DOWNLOADERVOGELVOERKOPEN_H
