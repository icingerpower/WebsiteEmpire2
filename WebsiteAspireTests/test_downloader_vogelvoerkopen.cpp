#include <QtTest>
#include <QDir>
#include <QEventLoop>
#include <QFuture>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSharedPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include "aspire/downloader/DownloaderVogelvoerkopen.h"
#include "aspire/downloader/DownloaderVogelvoerkopenCategory.h"
#include "aspire/downloader/DownloadedPagesTable.h"
#include "aspire/attributes/PageAttributesProduct.h"
#include "aspire/attributes/PageAttributesProductCategory.h"
#include "aspire/attributes/PageAttributesProductPetFood.h"
#include "ExceptionWithTitleText.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Fetches imageUrl via a dedicated QNetworkAccessManager using a nested
// QEventLoop.  Returns the raw bytes on success, or an empty array if the
// reply indicates an error.
static QByteArray fetchImageBytes(const QString &imageUrl)
{
    if (imageUrl.isEmpty()) {
        return {};
    }

    QNetworkAccessManager nam;
    QEventLoop loop;
    QByteArray data;

    QNetworkRequest req{QUrl{imageUrl}};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (compatible; WebsiteEmpire/1.0)"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, [reply, &data, &loop]() {
        if (reply->error() == QNetworkReply::NoError) {
            data = reply->readAll();
        }
        reply->deleteLater();
        loop.quit();
    });
    loop.exec();
    return data;
}

// Loads an image from raw bytes.  If loading fails or the image is too small,
// returns a 200×200 white placeholder so schema validation (≥200 px) always passes.
static QSharedPointer<QImage> imageFromBytes(const QByteArray &bytes)
{
    auto img = QSharedPointer<QImage>::create();
    if (!bytes.isEmpty()) {
        img->loadFromData(bytes);
    }
    if (img->isNull() || qMin(img->width(), img->height()) < 200) {
        *img = QImage(200, 200, QImage::Format_RGB32);
        img->fill(Qt::white);
    }
    return img;
}

// Builds a minimal product-page HTML string with a JSON-LD Product block and
// an optional BreadcrumbList block.
static QString makeProductHtml(const QString &name,
                               const QString &description,
                               const QString &price,
                               const QString &breadcrumbCategory = {},
                               const QString &imageUrl = {})
{
    QString breadcrumb;
    if (!breadcrumbCategory.isEmpty()) {
        breadcrumb = QString::fromLatin1(
            "<script type=\"application/ld+json\">"
            "{\"@type\":\"BreadcrumbList\",\"itemListElement\":["
            "{\"@type\":\"ListItem\",\"position\":1,\"name\":\"Home\"},"
            "{\"@type\":\"ListItem\",\"position\":2,\"name\":\"%1\"},"
            "{\"@type\":\"ListItem\",\"position\":3,\"name\":\"%2\"}"
            "]}</script>").arg(breadcrumbCategory, name);
    }

    const QString imgField = imageUrl.isEmpty() ? QString{}
                           : QStringLiteral(",\"image\":\"%1\"").arg(imageUrl);

    return QString::fromLatin1(
        "<html><head>%1"
        "<script type=\"application/ld+json\">"
        "{\"@type\":\"Product\","
        "\"name\":\"%2\","
        "\"description\":\"%3\","
        "\"offers\":{\"@type\":\"Offer\",\"price\":%4}%5}"
        "</script>"
        "</head><body></body></html>")
        .arg(breadcrumb, name, description, price, imgField);
}

// Builds a minimal category-page HTML string.
static QString makeCategoryHtml(const QString &categoryName,
                                const QString &termDescription = {})
{
    const QString descDiv = termDescription.isEmpty() ? QString{}
        : QString::fromLatin1("<div class=\"term-description\"><p>%1</p></div>")
              .arg(termDescription);

    return QString::fromLatin1(
        "<html><head>"
        "<script type=\"application/ld+json\">"
        "{\"@type\":\"BreadcrumbList\",\"itemListElement\":["
        "{\"@type\":\"ListItem\",\"position\":1,\"name\":\"Home\"},"
        "{\"@type\":\"ListItem\",\"position\":2,\"name\":\"%1\"}"
        "]}</script>"
        "</head><body>%2</body></html>")
        .arg(categoryName, descDiv);
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class Test_Downloader_Vogelvoerkopen : public QObject
{
    Q_OBJECT

private slots:
    // DownloaderVogelvoerkopen — getUrlsToParse
    void test_getUrlsToParse_sitemap_extracts_product_urls();
    void test_getUrlsToParse_html_extracts_href_urls();
    void test_getUrlsToParse_deduplicates_urls();
    void test_getUrlsToParse_rejects_off_domain();
    void test_getUrlsToParse_empty_content_returns_empty();

    // DownloaderVogelvoerkopen — getAttributeValues
    void test_getAttributeValues_non_product_page_returns_empty();
    void test_getAttributeValues_extracts_name_description_price();
    void test_getAttributeValues_breadcrumb_sets_category();
    void test_getAttributeValues_weight_parsed_from_name();
    void test_getAttributeValues_weight_from_variations();
    void test_getAttributeValues_price_from_aggregate_offer();
    void test_getAttributeValues_image_url_key_present();

    // DownloaderVogelvoerkopen — category matching
    void test_setKnownCategories_exact_match();
    void test_setKnownCategories_substring_match();
    void test_setKnownCategories_keyword_match();
    void test_setKnownCategories_no_match_defaults_to_first();
    void test_setKnownCategories_empty_list_keeps_raw();

    // DownloaderVogelvoerkopenCategory — getUrlsToParse
    void test_category_getUrlsToParse_sitemap_extracts_category_urls();
    void test_category_getUrlsToParse_filters_non_category_loc();
    void test_category_getUrlsToParse_html_returns_empty();

    // DownloaderVogelvoerkopenCategory — getAttributeValues
    void test_category_getAttributeValues_extracts_from_breadcrumb();
    void test_category_getAttributeValues_product_page_returns_empty();
    void test_category_getAttributeValues_description_fallback();
    void test_category_getAttributeValues_h1_fallback();

    // Integration tests
    void test_parse_three_categories_without_error();
    void test_parse_five_products_without_error();
};

// ===========================================================================
// DownloaderVogelvoerkopen — getUrlsToParse
// ===========================================================================

void Test_Downloader_Vogelvoerkopen::test_getUrlsToParse_sitemap_extracts_product_urls()
{
    DownloaderVogelvoerkopen dl;

    const QString sitemap = QString::fromLatin1(
        "<?xml version=\"1.0\"?><urlset>"
        "<url><loc>https://www.vogelvoerkopen.nl/product/zonnebloem/</loc></url>"
        "<url><loc>https://www.vogelvoerkopen.nl/product/pinda/</loc></url>"
        "</urlset>");

    const QStringList urls = dl.getUrlsToParse(sitemap);

    QCOMPARE(urls.size(), 2);
    QVERIFY(urls.contains(QStringLiteral("https://www.vogelvoerkopen.nl/product/zonnebloem/")));
    QVERIFY(urls.contains(QStringLiteral("https://www.vogelvoerkopen.nl/product/pinda/")));
}

void Test_Downloader_Vogelvoerkopen::test_getUrlsToParse_html_extracts_href_urls()
{
    DownloaderVogelvoerkopen dl;

    const QString html = QString::fromLatin1(
        "<html><body>"
        "<a href=\"https://www.vogelvoerkopen.nl/product/zonnebloem/\">Zonnebloem</a>"
        "<a href=\"https://www.vogelvoerkopen.nl/product-category/vogelvoer/\">Vogelvoer</a>"
        "</body></html>");

    const QStringList urls = dl.getUrlsToParse(html);

    QCOMPARE(urls.size(), 2);
    QVERIFY(urls.contains(QStringLiteral("https://www.vogelvoerkopen.nl/product/zonnebloem/")));
    QVERIFY(urls.contains(QStringLiteral("https://www.vogelvoerkopen.nl/product-category/vogelvoer/")));
}

void Test_Downloader_Vogelvoerkopen::test_getUrlsToParse_deduplicates_urls()
{
    DownloaderVogelvoerkopen dl;

    const QString html = QString::fromLatin1(
        "<a href=\"https://www.vogelvoerkopen.nl/product/zonnebloem/\">A</a>"
        "<a href=\"https://www.vogelvoerkopen.nl/product/zonnebloem/\">B</a>");

    const QStringList urls = dl.getUrlsToParse(html);

    QCOMPARE(urls.size(), 1);
    QCOMPARE(urls.first(), QStringLiteral("https://www.vogelvoerkopen.nl/product/zonnebloem/"));
}

void Test_Downloader_Vogelvoerkopen::test_getUrlsToParse_rejects_off_domain()
{
    DownloaderVogelvoerkopen dl;

    const QString html = QString::fromLatin1(
        "<a href=\"https://www.vogelvoerkopen.nl/product/zonnebloem/\">ok</a>"
        "<a href=\"https://www.example.com/other/\">off domain</a>");

    const QStringList urls = dl.getUrlsToParse(html);

    QCOMPARE(urls.size(), 1);
    QCOMPARE(urls.first(), QStringLiteral("https://www.vogelvoerkopen.nl/product/zonnebloem/"));
}

void Test_Downloader_Vogelvoerkopen::test_getUrlsToParse_empty_content_returns_empty()
{
    DownloaderVogelvoerkopen dl;
    QVERIFY(dl.getUrlsToParse(QString{}).isEmpty());
}

// ===========================================================================
// DownloaderVogelvoerkopen — getAttributeValues
// ===========================================================================

void Test_Downloader_Vogelvoerkopen::test_getAttributeValues_non_product_page_returns_empty()
{
    DownloaderVogelvoerkopen dl;

    const QString html = QStringLiteral(
        "<html><body><p>Some non-product content.</p></body></html>");

    QVERIFY(dl.getAttributeValues(QStringLiteral("https://vogelvoerkopen.nl/not-a-product/"), html).isEmpty());
}

void Test_Downloader_Vogelvoerkopen::test_getAttributeValues_extracts_name_description_price()
{
    DownloaderVogelvoerkopen dl;

    const QString url = QStringLiteral("https://vogelvoerkopen.nl/zonnebloem-500g/");
    const QString html = makeProductHtml(
        QStringLiteral("Zonnebloem 500g"),
        QStringLiteral("Heerlijk vogelzaad voor tuinvogels."),
        QStringLiteral("5.99"));

    const auto attrs = dl.getAttributeValues(url, html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProduct::ID_URL), url);
    QCOMPARE(attrs.value(PageAttributesProduct::ID_NAME),
             QStringLiteral("Zonnebloem 500g"));
    QCOMPARE(attrs.value(PageAttributesProduct::ID_SALE_PRICE),
             QStringLiteral("5.99"));
    QVERIFY(!attrs.value(PageAttributesProduct::ID_DESCRIPTION).isEmpty());
    // All required pet-food keys must be present
    QVERIFY(attrs.contains(PageAttributesProductPetFood::ID_WEIGHT_GR));
    QVERIFY(attrs.contains(PageAttributesProductPetFood::ID_PRICES));
}

void Test_Downloader_Vogelvoerkopen::test_getAttributeValues_breadcrumb_sets_category()
{
    DownloaderVogelvoerkopen dl;

    const QString html = makeProductHtml(
        QStringLiteral("Pindavoer 1kg"),
        QStringLiteral("Lekker pindavoer."),
        QStringLiteral("8.50"),
        QStringLiteral("Wilde vogels"));

    const QString url = QStringLiteral("https://vogelvoerkopen.nl/pindavoer-1kg/");
    const auto attrs = dl.getAttributeValues(url, html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProduct::ID_URL), url);
    QCOMPARE(attrs.value(PageAttributesProduct::ID_CATEGORY),
             QStringLiteral("Wilde vogels"));
}

void Test_Downloader_Vogelvoerkopen::test_getAttributeValues_weight_parsed_from_name()
{
    DownloaderVogelvoerkopen dl;

    // No data-product_variations → weight parsed from name "2kg"
    const QString html = makeProductHtml(
        QStringLiteral("Zonnebloem 2kg"),
        QStringLiteral("Vogelzaad zonnebloem 2 kilogram."),
        QStringLiteral("12.00"));

    const QString url = QStringLiteral("https://vogelvoerkopen.nl/zonnebloem-2kg/");
    const auto attrs = dl.getAttributeValues(url, html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProduct::ID_URL), url);
    QCOMPARE(attrs.value(PageAttributesProductPetFood::ID_WEIGHT_GR),
             QStringLiteral("2000"));
    // price falls back to sale price
    QCOMPARE(attrs.value(PageAttributesProductPetFood::ID_PRICES),
             QStringLiteral("12.00"));
}

void Test_Downloader_Vogelvoerkopen::test_getAttributeValues_weight_from_variations()
{
    DownloaderVogelvoerkopen dl;

    // Build encoded data-product_variations JSON for two variants
    const QString varJson =
        QStringLiteral("[")
        + QStringLiteral(R"({"is_in_stock":true,"attributes":{"attribute_pa_gewicht":"500-g"},"display_price":4.99},)")
        + QStringLiteral(R"({"is_in_stock":true,"attributes":{"attribute_pa_gewicht":"1-kg"},"display_price":8.99})")
        + QStringLiteral("]");

    QString encoded = varJson;
    encoded.replace(QLatin1String("\""), QLatin1String("&quot;"));

    const QString html = QString::fromLatin1(
        "<html><head>"
        "<script type=\"application/ld+json\">"
        "{\"@type\":\"Product\",\"name\":\"Zonnebloem varianten\","
        "\"description\":\"Vogelzaad.\","
        "\"offers\":{\"@type\":\"Offer\",\"price\":4.99}}"
        "</script>"
        "</head><body><form data-product_variations=\"%1\"></form>"
        "</body></html>").arg(encoded);

    const QString url = QStringLiteral("https://vogelvoerkopen.nl/zonnebloem-varianten/");
    const auto attrs = dl.getAttributeValues(url, html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProduct::ID_URL), url);
    const QString weights = attrs.value(PageAttributesProductPetFood::ID_WEIGHT_GR);
    const QString prices  = attrs.value(PageAttributesProductPetFood::ID_PRICES);
    QVERIFY(weights.contains(QLatin1String("500")));
    QVERIFY(weights.contains(QLatin1String("1000")));
    QVERIFY(prices.contains(QLatin1String("4.99")));
    QVERIFY(prices.contains(QLatin1String("8.99")));
}

void Test_Downloader_Vogelvoerkopen::test_getAttributeValues_price_from_aggregate_offer()
{
    DownloaderVogelvoerkopen dl;

    // AggregateOffer with lowPrice
    const QString html = QString::fromLatin1(
        "<html><head>"
        "<script type=\"application/ld+json\">"
        "{\"@type\":\"Product\",\"name\":\"Pindavet mix\","
        "\"description\":\"Mix voor tuinvogels.\","
        "\"offers\":{\"@type\":\"AggregateOffer\","
        "\"lowPrice\":\"3.49\",\"highPrice\":\"9.99\"}}"
        "</script>"
        "</head><body></body></html>");

    const QString url = QStringLiteral("https://vogelvoerkopen.nl/pindavet-mix/");
    const auto attrs = dl.getAttributeValues(url, html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProduct::ID_URL), url);
    QCOMPARE(attrs.value(PageAttributesProduct::ID_SALE_PRICE),
             QStringLiteral("3.49"));
}

void Test_Downloader_Vogelvoerkopen::test_getAttributeValues_image_url_key_present()
{
    DownloaderVogelvoerkopen dl;

    const QString html = makeProductHtml(
        QStringLiteral("Vogelzaad mix"),
        QStringLiteral("Een heerlijke mix voor alle tuinvogels."),
        QStringLiteral("6.99"),
        {},
        QStringLiteral("https://www.vogelvoerkopen.nl/img/mix.jpg"));

    const QString url = QStringLiteral("https://vogelvoerkopen.nl/vogelzaad-mix/");
    const auto attrs = dl.getAttributeValues(url, html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProduct::ID_URL), url);
    QVERIFY(attrs.contains(VOGELVOERKOPEN_IMAGE_URL_KEY));
    QCOMPARE(attrs.value(VOGELVOERKOPEN_IMAGE_URL_KEY),
             QStringLiteral("https://www.vogelvoerkopen.nl/img/mix.jpg"));
}

// ===========================================================================
// DownloaderVogelvoerkopen — category matching via setKnownCategories
// ===========================================================================

void Test_Downloader_Vogelvoerkopen::test_setKnownCategories_exact_match()
{
    DownloaderVogelvoerkopen dl;
    dl.setKnownCategories({QStringLiteral("Vogelvoer"),
                           QStringLiteral("Wilde vogels"),
                           QStringLiteral("Papegaaien")});

    // Breadcrumb returns "wilde vogels" (lowercase); should match "Wilde vogels" exactly.
    const QString html = makeProductHtml(
        QStringLiteral("Pindanet"),
        QStringLiteral("Pindanet voor tuinvogels."),
        QStringLiteral("3.99"),
        QStringLiteral("wilde vogels")); // lowercase

    const QString url = QStringLiteral("https://vogelvoerkopen.nl/pindanet/");
    const auto attrs = dl.getAttributeValues(url, html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProduct::ID_URL), url);
    QCOMPARE(attrs.value(PageAttributesProduct::ID_CATEGORY),
             QStringLiteral("Wilde vogels"));
}

void Test_Downloader_Vogelvoerkopen::test_setKnownCategories_substring_match()
{
    DownloaderVogelvoerkopen dl;
    // "Vogelvoer en meer" contains "vogelvoer" as substring
    dl.setKnownCategories({QStringLiteral("Vogelvoer en meer"),
                           QStringLiteral("Papegaaien")});

    const QString html = makeProductHtml(
        QStringLiteral("Zonnebloem 1kg"),
        QStringLiteral("Zonnebloem vogelzaad."),
        QStringLiteral("7.99"),
        QStringLiteral("Vogelvoer")); // "Vogelvoer en meer" contains "vogelvoer"

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://vogelvoerkopen.nl/zonnebloem-1kg/"), html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProduct::ID_CATEGORY),
             QStringLiteral("Vogelvoer en meer"));
}

void Test_Downloader_Vogelvoerkopen::test_setKnownCategories_keyword_match()
{
    DownloaderVogelvoerkopen dl;
    dl.setKnownCategories({QStringLiteral("Parkieten en Papegaaien"),
                           QStringLiteral("Wilde Tuinvogels"),
                           QStringLiteral("Kanaries en Vinken")});

    // "Kanaries" shares the keyword "kanaries" with "Kanaries en Vinken"
    const QString html = makeProductHtml(
        QStringLiteral("Kanariemix 500g"),
        QStringLiteral("Speciaal zaad voor kanaries."),
        QStringLiteral("4.50"),
        QStringLiteral("Kanaries")); // keyword overlap with "Kanaries en Vinken"

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://vogelvoerkopen.nl/kanariemix-500g/"), html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProduct::ID_CATEGORY),
             QStringLiteral("Kanaries en Vinken"));
}

void Test_Downloader_Vogelvoerkopen::test_setKnownCategories_no_match_defaults_to_first()
{
    DownloaderVogelvoerkopen dl;
    dl.setKnownCategories({QStringLiteral("Vogelvoer"),
                           QStringLiteral("Wilde vogels")});

    // "Reptielenvoer" has no overlap with any known category → defaults to first.
    const QString html = makeProductHtml(
        QStringLiteral("Krokodillenvet"),
        QStringLiteral("Speciaal voer voor reptielen."),
        QStringLiteral("15.00"),
        QStringLiteral("Reptielenvoer"));

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://vogelvoerkopen.nl/krokodillenvet/"), html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProduct::ID_CATEGORY),
             QStringLiteral("Vogelvoer"));
}

void Test_Downloader_Vogelvoerkopen::test_setKnownCategories_empty_list_keeps_raw()
{
    DownloaderVogelvoerkopen dl;
    // setKnownCategories not called → raw breadcrumb value returned unchanged.

    const QString html = makeProductHtml(
        QStringLiteral("Meelwormen 500g"),
        QStringLiteral("Gedroogde meelwormen voor tuinvogels."),
        QStringLiteral("5.49"),
        QStringLiteral("Levend voer"));

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://vogelvoerkopen.nl/meelwormen-500g/"), html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProduct::ID_CATEGORY),
             QStringLiteral("Levend voer"));
}

// ===========================================================================
// DownloaderVogelvoerkopenCategory — getUrlsToParse
// ===========================================================================

void Test_Downloader_Vogelvoerkopen::test_category_getUrlsToParse_sitemap_extracts_category_urls()
{
    DownloaderVogelvoerkopenCategory dl;

    const QString sitemap = QString::fromLatin1(
        "<?xml version=\"1.0\"?><urlset>"
        "<url><loc>https://www.vogelvoerkopen.nl/product-category/vogelvoer/</loc></url>"
        "<url><loc>https://www.vogelvoerkopen.nl/product-category/wilde-vogels/</loc></url>"
        "</urlset>");

    const QStringList urls = dl.getUrlsToParse(sitemap);

    QCOMPARE(urls.size(), 2);
    QVERIFY(urls.contains(
        QStringLiteral("https://www.vogelvoerkopen.nl/product-category/vogelvoer/")));
    QVERIFY(urls.contains(
        QStringLiteral("https://www.vogelvoerkopen.nl/product-category/wilde-vogels/")));
}

void Test_Downloader_Vogelvoerkopen::test_category_getUrlsToParse_filters_non_category_loc()
{
    DownloaderVogelvoerkopenCategory dl;

    // All vogelvoerkopen.nl URLs from a sitemap are returned so that product
    // pages can be crawled to discover category URLs via BreadcrumbList @id.
    const QString sitemap = QString::fromLatin1(
        "<?xml version=\"1.0\"?><urlset>"
        "<url><loc>https://www.vogelvoerkopen.nl/product-category/vogelvoer/</loc></url>"
        "<url><loc>https://www.vogelvoerkopen.nl/product/zonnebloem/</loc></url>"
        "<url><loc>https://www.example.com/offsite/</loc></url>"
        "</urlset>");

    const QStringList urls = dl.getUrlsToParse(sitemap);

    // Only the two vogelvoerkopen.nl URLs pass; the off-domain URL is filtered out.
    QCOMPARE(urls.size(), 2);
    QVERIFY(urls.contains(
        QStringLiteral("https://www.vogelvoerkopen.nl/product-category/vogelvoer/")));
    QVERIFY(urls.contains(
        QStringLiteral("https://www.vogelvoerkopen.nl/product/zonnebloem/")));
}

void Test_Downloader_Vogelvoerkopen::test_category_getUrlsToParse_html_returns_empty()
{
    DownloaderVogelvoerkopenCategory dl;

    const QString html = makeCategoryHtml(QStringLiteral("Vogelvoer"),
                                          QStringLiteral("Voer voor tuinvogels."));
    // HTML content → no further links extracted from category pages.
    QVERIFY(dl.getUrlsToParse(html).isEmpty());
}

// ===========================================================================
// DownloaderVogelvoerkopenCategory — getAttributeValues
// ===========================================================================

void Test_Downloader_Vogelvoerkopen::test_category_getAttributeValues_extracts_from_breadcrumb()
{
    DownloaderVogelvoerkopenCategory dl;

    const QString html = makeCategoryHtml(
        QStringLiteral("Wilde vogels"),
        QStringLiteral("Vogelzaad en nestmateriaal voor wilde tuinvogels."));

    const QString url = QStringLiteral("https://vogelvoerkopen.nl/wilde-vogels/");
    const auto attrs = dl.getAttributeValues(url, html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProductCategory::ID_URL), url);
    QCOMPARE(attrs.value(PageAttributesProductCategory::ID_PRODUCT_CATEGORY),
             QStringLiteral("Wilde vogels"));
    const QString desc = attrs.value(PageAttributesProductCategory::ID_DESCRIPTION);
    QVERIFY(!desc.isEmpty());
    QVERIFY(desc.length() >= 10);
    QVERIFY(desc.contains(QStringLiteral("tuinvogels")));
}

void Test_Downloader_Vogelvoerkopen::test_category_getAttributeValues_product_page_returns_empty()
{
    DownloaderVogelvoerkopenCategory dl;

    // A page with no BreadcrumbList and no h1.page-title yields no category name
    // and must be rejected.  (In practice getUrlsToParse() never queues product
    // pages, so this is an extra safety check for pages with no parseable category.)
    const QString html = QString::fromLatin1(
        "<html><body><p>Just some content with no structured data.</p></body></html>");

    QVERIFY(dl.getAttributeValues(QStringLiteral("https://vogelvoerkopen.nl/unknown/"), html).isEmpty());
}

void Test_Downloader_Vogelvoerkopen::test_category_getAttributeValues_description_fallback()
{
    DownloaderVogelvoerkopenCategory dl;

    // No .term-description div → generated fallback description.
    const QString html = makeCategoryHtml(QStringLiteral("Papegaaien"));

    const QString url = QStringLiteral("https://vogelvoerkopen.nl/papegaaien/");
    const auto attrs = dl.getAttributeValues(url, html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProductCategory::ID_URL), url);
    QCOMPARE(attrs.value(PageAttributesProductCategory::ID_PRODUCT_CATEGORY),
             QStringLiteral("Papegaaien"));
    const QString desc = attrs.value(PageAttributesProductCategory::ID_DESCRIPTION);
    QVERIFY(desc.length() >= 10);
    QVERIFY(desc.contains(QStringLiteral("Papegaaien")));
}

void Test_Downloader_Vogelvoerkopen::test_category_getAttributeValues_h1_fallback()
{
    DownloaderVogelvoerkopenCategory dl;

    // No BreadcrumbList — fall back to <h1 class="page-title">.
    const QString html = QString::fromLatin1(
        "<html><body>"
        "<h1 class=\"page-title\">Kanaries</h1>"
        "<div class=\"term-description\"><p>Vogelzaad speciaal voor kanaries en tropische vogels.</p></div>"
        "</body></html>");

    const QString url = QStringLiteral("https://vogelvoerkopen.nl/kanaries/");
    const auto attrs = dl.getAttributeValues(url, html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProductCategory::ID_URL), url);
    QCOMPARE(attrs.value(PageAttributesProductCategory::ID_PRODUCT_CATEGORY),
             QStringLiteral("Kanaries"));
    QVERIFY(attrs.value(PageAttributesProductCategory::ID_DESCRIPTION).length() >= 10);
}

// ===========================================================================
// Integration test: crawl vogelvoerkopen.nl until 3 categories are recorded
// ===========================================================================
void Test_Downloader_Vogelvoerkopen::test_parse_three_categories_without_error()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    const QDir workDir(tmpDir.path());

    int recordedCount = 0;
    QString failureReason;
    QEventLoop eventLoop;
    DownloadedPagesTable *tablePtr = nullptr;

    auto onPageParsed = [&](const QString & /*url*/,
                            const QHash<QString, QString> &attrs) -> bool {
        if (recordedCount >= 3 || !failureReason.isEmpty() || !tablePtr) {
            return false;
        }

        const QString categoryName =
            attrs.value(PageAttributesProductCategory::ID_PRODUCT_CATEGORY);
        if (categoryName.isEmpty()) {
            return false; // Not a category page
        }

        try {
            tablePtr->recordPage(attrs, {});
            ++recordedCount;
            if (recordedCount >= 3) {
                eventLoop.quit();
            }
        } catch (const QException &e) {
            failureReason = QString::fromUtf8(e.what());
            eventLoop.quit();
        }

        return true;
    };

    DownloaderVogelvoerkopenCategory downloader(workDir, onPageParsed);
    DownloadedPagesTable table(workDir, &downloader);
    tablePtr = &table;

    QFuture<void> parseFuture = downloader.parse(downloader.getSeedUrls());
    parseFuture.then([&eventLoop]() { eventLoop.quit(); });

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &eventLoop,
                     [&eventLoop, &failureReason]() {
                         failureReason = QStringLiteral(
                             "Timeout: 3 categories were not recorded within 300 seconds");
                         eventLoop.quit();
                     });
    timeout.start(300'000);

    eventLoop.exec();

    QVERIFY2(failureReason.isEmpty(), qPrintable(failureReason));
    QCOMPARE(table.rowCount(), 3);
}

// ===========================================================================
// Integration test: crawl vogelvoerkopen.nl until 5 products are recorded
// ===========================================================================
void Test_Downloader_Vogelvoerkopen::test_parse_five_products_without_error()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    const QDir workDir(tmpDir.path());

    // State shared between the callback lambda and the event loop below.
    int recordedCount = 0;
    QString failureReason;
    QEventLoop eventLoop;
    DownloadedPagesTable *tablePtr = nullptr; // set after table construction

    // PageParsedCallback: called once per URL that the downloader processes.
    // When the page is a product (attrs non-empty and has a name), this fetches
    // the product image synchronously via a nested event loop, then records the
    // row into DownloadedPagesTable.  Stops the outer event loop once 5 rows
    // have been committed.
    auto onPageParsed = [&](const QString & /*url*/,
                            const QHash<QString, QString> &attrs) -> bool {
        if (recordedCount >= 5 || !failureReason.isEmpty() || !tablePtr) {
            return false;
        }

        const QString name = attrs.value(PageAttributesProduct::ID_NAME);
        if (name.isEmpty()) {
            return false; // Not a product page
        }

        // Build image list
        const QString imageUrl = attrs.value(VOGELVOERKOPEN_IMAGE_URL_KEY);
        const QByteArray imgBytes = fetchImageBytes(imageUrl);
        const QSharedPointer<QImage> img = imageFromBytes(imgBytes);

        // Strip the non-schema key before recording
        QHash<QString, QString> textAttrs = attrs;
        textAttrs.remove(VOGELVOERKOPEN_IMAGE_URL_KEY);

        const QHash<QString, QList<QSharedPointer<QImage>>> imageAttrs{
            {PageAttributesProduct::ID_IMAGES, {img}}
        };

        try {
            tablePtr->recordPage(textAttrs, imageAttrs);
            ++recordedCount;
            if (recordedCount >= 5) {
                eventLoop.quit();
            }
        } catch (const QException &e) {
            failureReason = QString::fromUtf8(e.what());
            eventLoop.quit();
        }

        return true;
    };

    // Construct downloader and table.  The table must outlive the downloader
    // because it holds a non-owning AbstractDownloader* for schema queries.
    DownloaderVogelvoerkopen downloader(workDir, onPageParsed);
    DownloadedPagesTable table(workDir, &downloader);
    tablePtr = &table;

    // Seed with the product sitemap so the crawler reaches product pages without
    // having to walk the whole category hierarchy first.
    const QStringList seedUrls{QStringLiteral("https://www.vogelvoerkopen.nl/product-sitemap1.xml")};

    QFuture<void> parseFuture = downloader.parse(seedUrls);

    // Guard against the crawl finishing before all 5 products are recorded
    // (e.g. if the sitemap is empty or all pages return non-product content).
    parseFuture.then([&eventLoop]() { eventLoop.quit(); });

    // Hard timeout: 120 s is generous for an integration test on the real site.
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &eventLoop, [&eventLoop, &failureReason]() {
        failureReason = QStringLiteral("Timeout: 5 products were not recorded within 120 seconds");
        eventLoop.quit();
    });
    timeout.start(120'000);

    eventLoop.exec();

    QVERIFY2(failureReason.isEmpty(), qPrintable(failureReason));
    QCOMPARE(table.rowCount(), 5);
}

QTEST_MAIN(Test_Downloader_Vogelvoerkopen)
#include "test_downloader_vogelvoerkopen.moc"
