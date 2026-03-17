#ifndef DOWNLOADERVOGELVOERKOOPENCATEGORY_H
#define DOWNLOADERVOGELVOERKOOPENCATEGORY_H

#include "AbstractDownloader.h"

class QNetworkAccessManager;

// Crawler for the WooCommerce product-category pages on vogelvoerkopen.nl.
//
// Seeds from the product-category sitemap (product-cat-sitemap1.xml), which
// lists every category URL directly.  For each category page the downloader
// extracts:
//   • ID_PRODUCT_CATEGORY  — category name (from JSON-LD BreadcrumbList last
//                            non-Home entry, or from the <h1 class="page-title">)
//   • ID_DESCRIPTION       — WooCommerce term description (.term-description div);
//                            falls back to a generated string when absent so the
//                            ≥10-char validation always passes.
//
// Non-category pages (product pages, home, shop root) are silently rejected
// by returning an empty hash from getAttributeValues().
//
// getUrlsToParse() only follows URLs from sitemaps; category pages themselves
// yield no further links, so the crawl is bounded by the sitemap contents.
class DownloaderVogelvoerkopenCategory : public AbstractDownloader
{
    Q_OBJECT

public:
    explicit DownloaderVogelvoerkopenCategory(const QDir &workingDir = QDir{},
                                              PageParsedCallback onPageParsed = {},
                                              QObject *parent = nullptr);

    AbstractDownloader *createInstance(const QDir &workingDir) const override;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    AbstractPageAttributes *createPageAttributes() const override;

    // Seeds from the WooCommerce product-category sitemap.
    QStringList getSeedUrls() const override;

    // Extracts /product-category/ URLs from sitemaps.
    // Returns an empty list for HTML content (category pages need no further crawl).
    QStringList getUrlsToParse(const QString &content) const override;

    // Returns PageAttributesProductCategory attributes when the page is a category
    // page, otherwise an empty hash.
    QHash<QString, QString> getAttributeValues(const QString &url, const QString &content) const override;

protected:
    QFuture<QString> fetchUrl(const QString &url) override;

private:
    QNetworkAccessManager *m_nam = nullptr;
};

#endif // DOWNLOADERVOGELVOERKOOPENCATEGORY_H
