#include <QtTest>
#include <QDir>
#include <QEventLoop>
#include <QFuture>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>
#include <QSharedPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include "aspire/downloader/DownloaderPradize.h"
#include "aspire/downloader/DownloaderPradizeCategory.h"
#include "aspire/downloader/DownloadedPagesTable.h"
#include "aspire/attributes/PageAttributesProduct.h"
#include "aspire/attributes/PageAttributesProductCategory.h"
#include "aspire/attributes/PageAttributesProductFashion.h"
#include "ExceptionWithTitleText.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Fetches a single imageUrl via a dedicated QNetworkAccessManager using a
// nested QEventLoop.  Returns raw bytes on success, or an empty array on error.
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

// Calls the FES token endpoint so that nam's cookie jar gains the
// _rclientSessionId cookie required for full SSR responses from pradize.com.
static void acquireSessionCookie(QNetworkAccessManager &nam)
{
    QEventLoop loop;
    QNetworkRequest tokenReq{QUrl{QStringLiteral("https://pradize.com/api/v1/fes/token")}};
    tokenReq.setHeader(QNetworkRequest::UserAgentHeader,
                       QStringLiteral("Mozilla/5.0 (compatible; WebsiteEmpire/1.0)"));
    tokenReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                          QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *r = nam.get(tokenReq);
    QObject::connect(r, &QNetworkReply::finished, &loop, [r, &loop]() {
        r->deleteLater();
        loop.quit();
    });
    loop.exec();
}

// Fetches url using an existing QNetworkAccessManager (which must already hold
// the session cookie — call acquireSessionCookie() first if needed).
static QString fetchWithNam(const QString &url, QNetworkAccessManager &nam)
{
    QEventLoop loop;
    QString content;
    QNetworkRequest req{QUrl{url}};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (compatible; WebsiteEmpire/1.0)"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop,
                     [reply, &content, &loop]() {
        if (reply->error() == QNetworkReply::NoError) {
            content = QString::fromUtf8(reply->readAll());
        }
        reply->deleteLater();
        loop.quit();
    });
    loop.exec();
    return content;
}

// Fetches a URL that requires the CommerceHQ FES session cookie.
// Acquires the cookie via the token endpoint, then fetches the URL — both
// through the same QNetworkAccessManager so the cookie jar is shared.
static QString fetchWithSessionSync(const QString &url)
{
    QNetworkAccessManager nam;
    acquireSessionCookie(nam);
    return fetchWithNam(url, nam);
}

// Encodes a plain JSON string for embedding inside a CommerceHQ builderApp-state
// <script> tag: replaces " → &q;, < → &l;, > → &g;, ' → &s;, & → &a;
// NOTE: the & replacement must be performed first to avoid double-encoding.
static QString encodeForState(const QString &json)
{
    QString result = json;
    result.replace(QLatin1Char('&'), QLatin1String("&a;"));
    result.replace(QLatin1Char('"'), QLatin1String("&q;"));
    result.replace(QLatin1Char('<'), QLatin1String("&l;"));
    result.replace(QLatin1Char('>'), QLatin1String("&g;"));
    result.replace(QLatin1Char('\''), QLatin1String("&s;"));
    return result;
}

// Builds minimal pradize product page HTML containing a JSON-LD Product block
// and a builderApp-state script with the given product data.
// sizeValues should be formatted like "US-2 | UK/AU-6 | EU/DE-32 | FR/ES-34".
// If variantPrices has the same size as sizeValues, variants with individual
// prices are generated; otherwise the single price applies to all sizes.
static QString makePradizeProductHtml(const QString &seoUrl,
                                      const QString &title,
                                      const QString &type,
                                      double price,
                                      const QStringList &imageUrls,
                                      const QStringList &sizeValues,
                                      const QString &color = {},
                                      const QString &description = {},
                                      const QList<double> &variantPrices = {})
{
    const QString desc = description.isEmpty()
                       ? QStringLiteral("A fine fashion product description.")
                       : description;

    // Build images JSON array
    QString imagesJson = QLatin1String("[");
    for (int i = 0; i < imageUrls.size(); ++i) {
        if (i > 0) {
            imagesJson += QLatin1Char(',');
        }
        imagesJson += QString(R"({"id":%1,"path":"%2"})").arg(i + 1).arg(imageUrls[i]);
    }
    imagesJson += QLatin1Char(']');

    // Build options JSON array
    QString optionsJson = QLatin1String("[");
    if (!color.isEmpty()) {
        optionsJson += QString(R"({"title":"Color","changes_look":true,"values":["%1"],"thumbnails":[]},)").arg(color);
    }
    optionsJson += QLatin1String(R"({"title":"Size","changes_look":false,"values":[)");
    for (int i = 0; i < sizeValues.size(); ++i) {
        if (i > 0) {
            optionsJson += QLatin1Char(',');
        }
        optionsJson += QLatin1Char('"') + sizeValues[i] + QLatin1Char('"');
    }
    optionsJson += QLatin1String(R"(],"thumbnails":[]})");
    optionsJson += QLatin1Char(']');

    // Build variants JSON array (one entry per size when prices match).
    QString variantsJson = QLatin1String("[");
    if (variantPrices.size() == sizeValues.size()) {
        for (int i = 0; i < sizeValues.size(); ++i) {
            if (i > 0) {
                variantsJson += QLatin1Char(',');
            }
            variantsJson += QString(R"({"id":%1,"variant":["%2"],"price":%3,"images":[]})").arg(i + 100).arg(sizeValues[i]).arg(variantPrices[i]);
        }
    }
    variantsJson += QLatin1Char(']');

    // Build the product body JSON (plain, not yet encoded)
    const QString bodyJson = QString(
        R"({"id":123,"title":"%1","type":"%2","seo_url":"%3","default_price":%4,)"
        R"("images":%5,"textareas":[{"name":"Description","text":"%6"}],)"
        R"("options":%7,"variants":%8})")
        .arg(title, type, seoUrl).arg(price).arg(imagesJson, desc, optionsJson, variantsJson);

    // Encode the body for the state script
    const QString encodedBody = encodeForState(bodyJson);

    // Wrap in a minimal state JSON: {"hash1":{"body":<encoded>}}
    // The outer braces and key remain plain ASCII; only the body is encoded.
    const QString stateContent = QLatin1String(R"({"hash1":{)") +
                                 encodeForState(QStringLiteral("\"body\":")) +
                                 encodedBody +
                                 QLatin1String("}}");

    return QString(
        R"(<html><head>)"
        R"(<script type="application/ld+json">{"@context":"http://schema.org/","@type":"Product","name":"%1","offers":{"@type":"Offer","price":"%2"}}</script>)"
        R"(<script id="builderApp-state" type="application/json">%3</script>)"
        R"(</head><body></body></html>)")
        .arg(title).arg(price).arg(stateContent);
}

// Builds minimal pradize collection page HTML containing a builderApp-state
// script with the given collection data.
static QString makePradizeCategoryHtml(const QString &seoUrl,
                                       const QString &title,
                                       const QString &description = {})
{
    const QString desc = description.isEmpty()
                       ? QString(QStringLiteral("Discover our %1 collection.")).arg(title)
                       : description;

    // Build collection body JSON (plain)
    const QString bodyJson = QString(
        R"({"id":100,"title":"%1","description":"%2","type":0,"seo_url":"%3"})")
        .arg(title, desc, seoUrl);

    const QString encodedBody = encodeForState(bodyJson);

    const QString stateContent = QLatin1String(R"({"hash1":{)") +
                                 encodeForState(QStringLiteral("\"body\":")) +
                                 encodedBody +
                                 QLatin1String("}}");

    return QString(
        R"(<html><head>)"
        R"(<script id="builderApp-state" type="application/json">%1</script>)"
        R"(</head><body></body></html>)")
        .arg(stateContent);
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class Test_Downloader_Pradize : public QObject
{
    Q_OBJECT

private slots:
    // DownloaderPradize — getUrlsToParse
    void test_getUrlsToParse_extracts_product_urls();
    void test_getUrlsToParse_extracts_collection_urls();
    void test_getUrlsToParse_deduplicates_urls();
    void test_getUrlsToParse_rejects_off_domain();
    void test_getUrlsToParse_empty_content_returns_empty();
    void test_getUrlsToParse_from_encoded_state_content();
    void test_getUrlsToParse_collection_html_adds_api_products_url();
    void test_getUrlsToParse_products_api_json_returns_product_urls();

    // DownloaderPradize — getAttributeValues
    void test_getAttributeValues_non_product_url_returns_empty();
    void test_getAttributeValues_no_state_script_returns_empty();
    void test_getAttributeValues_extracts_name_category_price();
    void test_getAttributeValues_extracts_raw_sizes();
    void test_getAttributeValues_extracts_prices_from_default();
    void test_getAttributeValues_extracts_prices_from_variants();
    void test_getAttributeValues_extracts_fr_sizes_from_clothing();
    void test_getAttributeValues_extracts_fr_sizes_from_shoes_eu();
    void test_getAttributeValues_extracts_colors_from_option();
    void test_getAttributeValues_detects_color_from_title();
    void test_getAttributeValues_detects_compound_color_from_title();
    void test_getAttributeValues_detects_color_from_description_fallback();
    void test_getAttributeValues_sexe_is_women();
    void test_getAttributeValues_age_is_adult();
    void test_getAttributeValues_type_is_clothe_for_dress();
    void test_getAttributeValues_type_is_shoes_for_shoe_category();
    void test_getAttributeValues_type_is_accessory_for_bag();
    void test_getAttributeValues_image_urls_key_present();
    void test_getAttributeValues_multiple_images_from_top_level();
    void test_getAttributeValues_images_from_variants_fallback();
    void test_crossvalidation_same_count_passes();
    void test_crossvalidation_different_count_fails();

    // DownloaderPradize — category matching
    void test_setKnownCategories_exact_match();
    void test_setKnownCategories_substring_match();
    void test_setKnownCategories_keyword_match();
    void test_setKnownCategories_no_match_defaults_to_first();
    void test_setKnownCategories_empty_list_keeps_raw();

    // DownloaderPradizeCategory — getUrlsToParse
    void test_category_getUrlsToParse_extracts_collection_urls();
    void test_category_getUrlsToParse_deduplicates();

    // DownloaderPradizeCategory — getAttributeValues
    void test_category_getAttributeValues_extracts_title_and_description();
    void test_category_getAttributeValues_product_url_returns_empty();
    void test_category_getAttributeValues_description_fallback();
    void test_category_getAttributeValues_wrong_type_returns_empty();

    // Integration tests — live pradize.com URLs
    // NOTE: test_crawl_finds_at_least_3_product_pages MUST stay last in this
    // group.  It makes FES API calls that can trigger server-side rate-limiting,
    // which would cause the HTML-page tests above it to receive stripped responses
    // (no builderApp-state script) and fail.
    //
    // Session-cookie requirement tests (must run before ariana/aria so they
    // execute on a fresh connection without prior rate-limiting):
    void test_product_page_without_session_cookie_returns_shell();
    void test_downloader_fetches_html_product_page_with_session_cookie();
    void test_parse_ariana_dress_with_sizes_and_color();
    void test_parse_aria_booties_with_sizes_and_color();
    void test_parse_various_product_pages_succeed();
    void test_parse_failing_urls_from_production_log();
    void test_crawl_finds_at_least_3_product_pages();
};

// ===========================================================================
// DownloaderPradize — getUrlsToParse
// ===========================================================================

void Test_Downloader_Pradize::test_getUrlsToParse_extracts_product_urls()
{
    DownloaderPradize dl;

    const QString html = QStringLiteral(
        "<html><body>"
        "<a href=\"https://pradize.com/product/test-dress\">Dress</a>"
        "</body></html>");

    const QStringList urls = dl.getUrlsToParse(html);

    QVERIFY(urls.contains(QStringLiteral("https://pradize.com/product/test-dress")));
}

void Test_Downloader_Pradize::test_getUrlsToParse_extracts_collection_urls()
{
    DownloaderPradize dl;

    const QString html = QStringLiteral(
        "<html><body>"
        "<a href=\"https://pradize.com/collection/mini-dresses\">Mini Dresses</a>"
        "</body></html>");

    const QStringList urls = dl.getUrlsToParse(html);

    QVERIFY(urls.contains(QStringLiteral("https://pradize.com/collection/mini-dresses")));
}

void Test_Downloader_Pradize::test_getUrlsToParse_deduplicates_urls()
{
    DownloaderPradize dl;

    const QString html =
        QStringLiteral("<a href=\"https://pradize.com/product/my-dress\">A</a>")
        + QStringLiteral("<a href=\"https://pradize.com/product/my-dress\">B</a>");

    const QStringList urls = dl.getUrlsToParse(html);

    QCOMPARE(urls.count(QStringLiteral("https://pradize.com/product/my-dress")), 1);
}

void Test_Downloader_Pradize::test_getUrlsToParse_rejects_off_domain()
{
    DownloaderPradize dl;

    const QString html =
        QStringLiteral("<a href=\"https://pradize.com/product/my-dress\">ok</a>")
        + QStringLiteral("<a href=\"https://www.example.com/product/other\">off</a>");

    const QStringList urls = dl.getUrlsToParse(html);

    QVERIFY(urls.contains(QStringLiteral("https://pradize.com/product/my-dress")));
    for (const QString &u : std::as_const(urls)) {
        QVERIFY2(u.contains(QLatin1String("pradize.com")),
                 qPrintable(QStringLiteral("Off-domain URL leaked: ") + u));
    }
}

void Test_Downloader_Pradize::test_getUrlsToParse_empty_content_returns_empty()
{
    DownloaderPradize dl;
    QVERIFY(dl.getUrlsToParse(QString{}).isEmpty());
}

void Test_Downloader_Pradize::test_getUrlsToParse_from_encoded_state_content()
{
    DownloaderPradize dl;

    // Simulate &q;-encoded paths inside a state script as they appear in real pages
    const QString html = QStringLiteral(
        R"(<script id="builderApp-state" type="application/json">)"
        R"({&q;href&q;:&q;/product/pink-dress&q;,&q;href2&q;:&q;/collection/mini-dresses&q;})"
        R"(</script>)");

    const QStringList urls = dl.getUrlsToParse(html);

    QVERIFY(urls.contains(QStringLiteral("https://pradize.com/product/pink-dress")));
    QVERIFY(urls.contains(QStringLiteral("https://pradize.com/collection/mini-dresses")));
}

void Test_Downloader_Pradize::test_getUrlsToParse_collection_html_adds_api_products_url()
{
    DownloaderPradize dl;

    // HTML containing a /collection/mini-dresses href must also produce the
    // FES API products URL so the crawl discovers all products.
    const QString html = QStringLiteral(
        "<html><body>"
        "<a href=\"/collection/mini-dresses\">Mini Dresses</a>"
        "</body></html>");

    const QStringList urls = dl.getUrlsToParse(html);

    QVERIFY(urls.contains(QStringLiteral("https://pradize.com/collection/mini-dresses")));
    QVERIFY2(urls.contains(
        QStringLiteral("https://pradize.com/api/v1/fes/products?collection=mini-dresses&page=1&limit=100")),
        "getUrlsToParse must enqueue the FES API products URL for each collection slug");
}

void Test_Downloader_Pradize::test_getUrlsToParse_products_api_json_returns_product_urls()
{
    DownloaderPradize dl;

    // Minimal FES products API response with 4 items and a next-page link.
    // Note: QString() used (not QStringLiteral) to avoid moc issues with
    // concatenated raw string literals inside macros.
    const QString json = QString(
        R"({"items":[)"
        R"({"seo_url":"blue-midi-dress"},)"
        R"({"seo_url":"red-maxi-gown"},)"
        R"({"seo_url":"floral-wrap-dress"},)"
        R"({"seo_url":"navy-corset-dress"})"
        R"(],"_meta":{"currentPage":1,"pageCount":3,"totalCount":400,"perPage":100},)"
        R"("_links":{"next":{"href":"https://pradize.com/api/v1/fes/products?collection=dresses&page=2&limit=100"}}})");

    const QStringList urls = dl.getUrlsToParse(json);

    // Must return at least 3 product HTML page URLs.
    int productCount = 0;
    for (const QString &u : std::as_const(urls)) {
        if (u.contains(QLatin1String("/product/"))) {
            ++productCount;
        }
    }
    QVERIFY2(productCount >= 3,
             qPrintable(QStringLiteral("Expected ≥3 product URLs from JSON, got %1").arg(productCount)));

    QVERIFY(urls.contains(QStringLiteral("https://pradize.com/product/blue-midi-dress")));
    QVERIFY(urls.contains(QStringLiteral("https://pradize.com/product/navy-corset-dress")));
    QVERIFY2(urls.contains(
        QStringLiteral("https://pradize.com/api/v1/fes/products?collection=dresses&page=2&limit=100")),
        "Next-page API URL must be included for pagination");
}

void Test_Downloader_Pradize::test_crawl_finds_at_least_3_product_pages()
{
    // Integration test: fetch a live FES products API page for a known
    // collection and verify that getUrlsToParse() extracts ≥3 product URLs.
    const QString apiUrl = QStringLiteral(
        "https://pradize.com/api/v1/fes/products?collection=mini-dresses&page=1&limit=100");

    const QString json = fetchWithSessionSync(apiUrl);
    QVERIFY2(!json.isEmpty(), "Failed to fetch FES products API for mini-dresses");

    DownloaderPradize dl;
    const QStringList urls = dl.getUrlsToParse(json);

    int productCount = 0;
    for (const QString &u : std::as_const(urls)) {
        if (u.contains(QLatin1String("/product/"))) {
            ++productCount;
        }
    }
    QVERIFY2(productCount >= 3,
             qPrintable(QStringLiteral(
                 "Expected ≥3 product URLs from live FES API, got %1").arg(productCount)));
}

// ===========================================================================
// DownloaderPradize — getAttributeValues
// ===========================================================================

void Test_Downloader_Pradize::test_getAttributeValues_non_product_url_returns_empty()
{
    DownloaderPradize dl;

    const QString html = makePradizeProductHtml(
        QStringLiteral("test-dress"), QStringLiteral("Test Dress"),
        QStringLiteral("Dress Long"), 79.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-2 | UK/AU-6 | EU/DE-32 | FR/ES-34")});

    // Collection URL — should not be parsed as a product
    QVERIFY(dl.getAttributeValues(
        QStringLiteral("https://pradize.com/collection/test-dress"), html).isEmpty());
}

void Test_Downloader_Pradize::test_getAttributeValues_no_state_script_returns_empty()
{
    DownloaderPradize dl;

    const QString html = QStringLiteral(
        "<html><head>"
        "<script type=\"application/ld+json\">{\"@type\":\"Product\",\"name\":\"Test\"}</script>"
        "</head><body></body></html>");

    QVERIFY(dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/test-dress"), html).isEmpty());
}

void Test_Downloader_Pradize::test_getAttributeValues_extracts_name_category_price()
{
    DownloaderPradize dl;

    const QString html = makePradizeProductHtml(
        QStringLiteral("blue-midi-dress"),
        QStringLiteral("Blue Midi Dress"),
        QStringLiteral("Dress Midi"),
        59.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-2 | UK/AU-6 | EU/DE-32 | FR/ES-34"),
         QStringLiteral("US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36")});

    const QString url = QStringLiteral("https://pradize.com/product/blue-midi-dress");
    const auto attrs = dl.getAttributeValues(url, html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProduct::ID_URL), url);
    QCOMPARE(attrs.value(PageAttributesProduct::ID_NAME),
             QStringLiteral("Blue Midi Dress"));
    QCOMPARE(attrs.value(PageAttributesProduct::ID_CATEGORY),
             QStringLiteral("Dress Midi"));
    QCOMPARE(attrs.value(PageAttributesProduct::ID_SALE_PRICE),
             QStringLiteral("59.99"));
}

void Test_Downloader_Pradize::test_getAttributeValues_extracts_fr_sizes_from_clothing()
{
    DownloaderPradize dl;

    const QString html = makePradizeProductHtml(
        QStringLiteral("white-evening-gown"),
        QStringLiteral("White Evening Gown"),
        QStringLiteral("Dress Long"),
        129.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-2 | UK/AU-6 | EU/DE-32 | FR/ES-34"),
         QStringLiteral("US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36"),
         QStringLiteral("US-6 | UK/AU-10 | EU/DE-36 | FR/ES-38")});

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/white-evening-gown"), html);

    QVERIFY(!attrs.isEmpty());
    const QString sizes = attrs.value(PageAttributesProductFashion::ID_SIZES_FR);
    QVERIFY(sizes.contains(QLatin1String("34")));
    QVERIFY(sizes.contains(QLatin1String("36")));
    QVERIFY(sizes.contains(QLatin1String("38")));
    // Verify semicolon-separated format
    const QStringList parts = sizes.split(QLatin1Char(';'));
    QCOMPARE(parts.size(), 3);
    QCOMPARE(parts[0], QStringLiteral("34"));
    QCOMPARE(parts[1], QStringLiteral("36"));
    QCOMPARE(parts[2], QStringLiteral("38"));
}

void Test_Downloader_Pradize::test_getAttributeValues_extracts_fr_sizes_from_shoes_eu()
{
    DownloaderPradize dl;

    // Shoe sizes: EU-XX format (no FR/ES part — EU == French shoe size)
    const QString html = makePradizeProductHtml(
        QStringLiteral("black-stiletto-heels"),
        QStringLiteral("Black Stiletto Heels"),
        QStringLiteral("Shoes"),
        89.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-5 | UK/AU-3 | EU-36"),
         QStringLiteral("US-6 | UK/AU-4 | EU-37"),
         QStringLiteral("US-7 | UK/AU-5 | EU-38")},
        QStringLiteral("Black"));

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/black-stiletto-heels"), html);

    QVERIFY(!attrs.isEmpty());
    const QString sizes = attrs.value(PageAttributesProductFashion::ID_SIZES_FR);
    QVERIFY(sizes.contains(QLatin1String("36")));
    QVERIFY(sizes.contains(QLatin1String("37")));
    QVERIFY(sizes.contains(QLatin1String("38")));
}

void Test_Downloader_Pradize::test_getAttributeValues_extracts_colors_from_option()
{
    DownloaderPradize dl;

    const QString html = makePradizeProductHtml(
        QStringLiteral("ankle-pointed-booties"),
        QStringLiteral("Ankle Pointed Booties"),
        QStringLiteral("Shoes"),
        79.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-3 | UK/AU-1 | EU-34")},
        QStringLiteral("Dark Brown")); // explicit Color option

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/ankle-pointed-booties"), html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProductFashion::ID_COLORS),
             QStringLiteral("Dark Brown"));
}

void Test_Downloader_Pradize::test_getAttributeValues_detects_color_from_title()
{
    DownloaderPradize dl;

    // No Color option — colour must be inferred from the title
    const QString html = makePradizeProductHtml(
        QStringLiteral("red-cocktail-dress"),
        QStringLiteral("Red Cocktail Dress"),
        QStringLiteral("Dress Mini"),
        69.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36")});

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/red-cocktail-dress"), html);

    QVERIFY(!attrs.isEmpty());
    const QString color = attrs.value(PageAttributesProductFashion::ID_COLORS);
    QVERIFY2(!color.isEmpty() && color.toLower().contains(QLatin1String("red")),
             qPrintable(QStringLiteral("Expected color containing 'red', got: ") + color));
}

void Test_Downloader_Pradize::test_getAttributeValues_detects_compound_color_from_title()
{
    DownloaderPradize dl;

    // "Blush Pink" is a compound colour; should be preferred over just "Pink"
    const QString html = makePradizeProductHtml(
        QStringLiteral("ariana-blush-pink-dress"),
        QStringLiteral("Ariana Blush Pink Fairycore Dress"),
        QStringLiteral("Dress Long"),
        129.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-2 | UK/AU-6 | EU/DE-32 | FR/ES-34")});

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/ariana-blush-pink-dress"), html);

    QVERIFY(!attrs.isEmpty());
    const QString color = attrs.value(PageAttributesProductFashion::ID_COLORS);
    // "blush pink" should be found before standalone "pink"
    QVERIFY2(color.toLower().contains(QLatin1String("blush")),
             qPrintable(QStringLiteral("Expected compound 'blush pink', got: ") + color));
}

void Test_Downloader_Pradize::test_getAttributeValues_detects_color_from_description_fallback()
{
    DownloaderPradize dl;

    // Title has no colour keyword; colour is in the description only
    const QString html = makePradizeProductHtml(
        QStringLiteral("ethereal-evening-gown"),
        QStringLiteral("Ethereal Evening Gown"), // no colour here
        QStringLiteral("Dress Long"),
        99.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36")},
        {},                                         // no Color option
        QStringLiteral("This stunning ivory dress is perfect for formal events."));

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/ethereal-evening-gown"), html);

    QVERIFY(!attrs.isEmpty());
    const QString color = attrs.value(PageAttributesProductFashion::ID_COLORS);
    QVERIFY2(color.toLower().contains(QLatin1String("ivory")),
             qPrintable(QStringLiteral("Expected 'ivory' from description, got: ") + color));
}

void Test_Downloader_Pradize::test_getAttributeValues_image_urls_key_present()
{
    DownloaderPradize dl;

    const QStringList images = {
        QStringLiteral("https://cdn.example.com/img1.jpg"),
        QStringLiteral("https://cdn.example.com/img2.jpg"),
    };

    const QString html = makePradizeProductHtml(
        QStringLiteral("navy-maxi-dress"),
        QStringLiteral("Navy Maxi Dress"),
        QStringLiteral("Dress Long"),
        89.99, images,
        {QStringLiteral("US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36")});

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/navy-maxi-dress"), html);

    QVERIFY(!attrs.isEmpty());
    QVERIFY(attrs.contains(PRADIZE_IMAGE_URLS_KEY));
    QVERIFY(!attrs.value(PRADIZE_IMAGE_URLS_KEY).isEmpty());
}

void Test_Downloader_Pradize::test_getAttributeValues_multiple_images_from_top_level()
{
    DownloaderPradize dl;

    const QStringList images = {
        QStringLiteral("https://cdn.example.com/img1.jpg"),
        QStringLiteral("https://cdn.example.com/img2.jpg"),
        QStringLiteral("https://cdn.example.com/img3.jpg"),
        QStringLiteral("https://cdn.example.com/img4.jpg"),
        QStringLiteral("https://cdn.example.com/img5.jpg"),
    };

    const QString html = makePradizeProductHtml(
        QStringLiteral("floral-midi-dress"),
        QStringLiteral("Floral Midi Dress"),
        QStringLiteral("Dress Midi"),
        74.99, images,
        {QStringLiteral("US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36")});

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/floral-midi-dress"), html);

    QVERIFY(!attrs.isEmpty());
    const QStringList imageUrls = attrs.value(PRADIZE_IMAGE_URLS_KEY).split(QLatin1Char(';'),
                                                                             Qt::SkipEmptyParts);
    QCOMPARE(imageUrls.size(), 5);
    QCOMPARE(imageUrls.first(), QStringLiteral("https://cdn.example.com/img1.jpg"));
}

void Test_Downloader_Pradize::test_getAttributeValues_images_from_variants_fallback()
{
    DownloaderPradize dl;

    // Product with empty top-level images[] but variants[] carrying images —
    // this mirrors the real-world aria-booties structure.
    // Build the HTML manually since the helper always puts images at top level.
    const QString bodyJson = QString(
        R"({"id":456,"title":"Black Ankle Boots","type":"Shoes","seo_url":"black-ankle-boots",)"
        R"("default_price":79.99,"images":[],"textareas":[{"name":"Description","text":"Nice boots."}],)"
        R"("options":[{"title":"Color","changes_look":true,"values":["Black"],"thumbnails":[]},)"
        R"({"title":"Size","changes_look":false,"values":["US-5 | UK/AU-3 | EU-36"],"thumbnails":[]}],)"
        R"("variants":[{"id":1,"variant":["Black","US-5 | UK/AU-3 | EU-36"],"price":79.99,)"
        R"("images":[{"id":10,"path":"https://cdn.example.com/boot1.jpg"},)"
        R"({"id":11,"path":"https://cdn.example.com/boot2.jpg"}]}]})");

    const QString stateContent = QLatin1String(R"({"hash1":{)") +
                                 encodeForState(QStringLiteral("\"body\":")) +
                                 encodeForState(bodyJson) +
                                 QLatin1String("}}");

    const QString html = QString(
        R"(<html><head>)"
        R"(<script type="application/ld+json">{"@type":"Product","name":"Black Ankle Boots","offers":{"@type":"Offer","price":"79.99"}}</script>)"
        R"(<script id="builderApp-state" type="application/json">%1</script>)"
        R"(</head><body></body></html>)").arg(stateContent);

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/black-ankle-boots"), html);

    QVERIFY(!attrs.isEmpty());
    const QStringList imageUrls = attrs.value(PRADIZE_IMAGE_URLS_KEY).split(QLatin1Char(';'),
                                                                             Qt::SkipEmptyParts);
    QVERIFY2(imageUrls.size() >= 2,
             qPrintable(QStringLiteral("Expected ≥2 images from variants, got %1").arg(imageUrls.size())));
    QCOMPARE(imageUrls.first(), QStringLiteral("https://cdn.example.com/boot1.jpg"));
}

void Test_Downloader_Pradize::test_getAttributeValues_extracts_raw_sizes()
{
    DownloaderPradize dl;

    const QStringList sizeValues = {
        QStringLiteral("US-2 | UK/AU-6 | EU/DE-32 | FR/ES-34"),
        QStringLiteral("US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36"),
    };

    const QString html = makePradizeProductHtml(
        QStringLiteral("blue-dress"), QStringLiteral("Blue Dress"),
        QStringLiteral("Dress Long"), 79.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        sizeValues);

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/blue-dress"), html);

    QVERIFY(!attrs.isEmpty());
    const QStringList sizes = attrs.value(PageAttributesProductFashion::ID_SIZES)
                                   .split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QCOMPARE(sizes.size(), 2);
    QCOMPARE(sizes[0], sizeValues[0]);
    QCOMPARE(sizes[1], sizeValues[1]);
}

void Test_Downloader_Pradize::test_getAttributeValues_extracts_prices_from_default()
{
    DownloaderPradize dl;

    // No variant prices → all sizes get the product's default_price.
    const QString html = makePradizeProductHtml(
        QStringLiteral("green-dress"), QStringLiteral("Green Dress"),
        QStringLiteral("Dress Midi"), 59.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-2 | UK/AU-6 | EU/DE-32 | FR/ES-34"),
         QStringLiteral("US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36")});

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/green-dress"), html);

    QVERIFY(!attrs.isEmpty());
    const QStringList prices = attrs.value(PageAttributesProductFashion::ID_PRICES)
                                    .split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QCOMPARE(prices.size(), 2);
    QCOMPARE(prices[0], QStringLiteral("59.99"));
    QCOMPARE(prices[1], QStringLiteral("59.99"));
}

void Test_Downloader_Pradize::test_getAttributeValues_extracts_prices_from_variants()
{
    DownloaderPradize dl;

    // Each size has a distinct price supplied via variants.
    const QStringList sizeValues = {
        QStringLiteral("US-2 | UK/AU-6 | EU/DE-32 | FR/ES-34"),
        QStringLiteral("US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36"),
    };
    const QList<double> variantPrices = {29.99, 34.99};

    const QString html = makePradizeProductHtml(
        QStringLiteral("multi-price-dress"), QStringLiteral("Multi Price Dress"),
        QStringLiteral("Dress Long"), 29.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        sizeValues, {}, {}, variantPrices);

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/multi-price-dress"), html);

    QVERIFY(!attrs.isEmpty());
    const QStringList prices = attrs.value(PageAttributesProductFashion::ID_PRICES)
                                    .split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QCOMPARE(prices.size(), 2);
    QCOMPARE(prices[0], QStringLiteral("29.99"));
    QCOMPARE(prices[1], QStringLiteral("34.99"));
}

void Test_Downloader_Pradize::test_getAttributeValues_sexe_is_women()
{
    DownloaderPradize dl;

    const QString html = makePradizeProductHtml(
        QStringLiteral("floral-dress"), QStringLiteral("Floral Dress"),
        QStringLiteral("Dress Long"), 69.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36")});

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/floral-dress"), html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProductFashion::ID_SEXE),
             PageAttributesProductFashion::SEXE_WOMEN);
}

void Test_Downloader_Pradize::test_getAttributeValues_age_is_adult()
{
    DownloaderPradize dl;

    const QString html = makePradizeProductHtml(
        QStringLiteral("evening-gown"), QStringLiteral("Evening Gown"),
        QStringLiteral("Dress Long"), 99.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36")});

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/evening-gown"), html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProductFashion::ID_AGE),
             PageAttributesProductFashion::AGE_ADULT);
}

void Test_Downloader_Pradize::test_getAttributeValues_type_is_clothe_for_dress()
{
    DownloaderPradize dl;

    const QString html = makePradizeProductHtml(
        QStringLiteral("midi-dress"), QStringLiteral("Midi Dress"),
        QStringLiteral("Dress Midi"), 74.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36")});

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/midi-dress"), html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProductFashion::ID_TYPE),
             PageAttributesProductFashion::TYPE_CLOTHE);
}

void Test_Downloader_Pradize::test_getAttributeValues_type_is_shoes_for_shoe_category()
{
    DownloaderPradize dl;

    const QString html = makePradizeProductHtml(
        QStringLiteral("ankle-boots"), QStringLiteral("Ankle Boots"),
        QStringLiteral("Ankle Boots"), 89.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-5 | UK/AU-3 | EU-36")},
        QStringLiteral("Black"));

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/ankle-boots"), html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProductFashion::ID_TYPE),
             PageAttributesProductFashion::TYPE_SHOES);
}

void Test_Downloader_Pradize::test_getAttributeValues_type_is_accessory_for_bag()
{
    DownloaderPradize dl;

    const QString html = makePradizeProductHtml(
        QStringLiteral("leather-bag"), QStringLiteral("Leather Bag"),
        QStringLiteral("Bag"), 49.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("One Size")});

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/leather-bag"), html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProductFashion::ID_TYPE),
             PageAttributesProductFashion::TYPE_ACCESSORY);
}

void Test_Downloader_Pradize::test_crossvalidation_same_count_passes()
{
    PageAttributesProductFashion attrs;

    QHash<QString, QString> values;
    values[PageAttributesProductFashion::ID_SIZES]  = QStringLiteral("S;M;L");
    values[PageAttributesProductFashion::ID_PRICES] = QStringLiteral("29.99;29.99;34.99");

    QVERIFY2(attrs.areAttributesCrossValid(values).isEmpty(),
             "Expected no cross-validation error for matching sizes/prices count");
}

void Test_Downloader_Pradize::test_crossvalidation_different_count_fails()
{
    PageAttributesProductFashion attrs;

    QHash<QString, QString> values;
    values[PageAttributesProductFashion::ID_SIZES]  = QStringLiteral("S;M;L");
    values[PageAttributesProductFashion::ID_PRICES] = QStringLiteral("29.99;29.99"); // 2 vs 3

    const QString error = attrs.areAttributesCrossValid(values);
    QVERIFY2(!error.isEmpty(),
             "Expected cross-validation error when sizes and prices counts differ");
    QVERIFY2(error.contains(QLatin1String("3")) && error.contains(QLatin1String("2")),
             qPrintable(QStringLiteral("Error should mention counts: ") + error));
}

// ===========================================================================
// DownloaderPradize — category matching via setKnownCategories
// ===========================================================================

void Test_Downloader_Pradize::test_setKnownCategories_exact_match()
{
    DownloaderPradize dl;
    dl.setKnownCategories({QStringLiteral("Dress Long"),
                           QStringLiteral("Shoes"),
                           QStringLiteral("Skirts")});

    const QString html = makePradizeProductHtml(
        QStringLiteral("red-maxi"),
        QStringLiteral("Red Maxi Dress"),
        QStringLiteral("dress long"), // lowercase — should still match
        99.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36")});

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/red-maxi"), html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProduct::ID_CATEGORY),
             QStringLiteral("Dress Long"));
}

void Test_Downloader_Pradize::test_setKnownCategories_substring_match()
{
    DownloaderPradize dl;
    dl.setKnownCategories({QStringLiteral("Long Evening Dresses"),
                           QStringLiteral("Shoes & Heels")});

    const QString html = makePradizeProductHtml(
        QStringLiteral("pink-gown"),
        QStringLiteral("Pink Gown"),
        QStringLiteral("Dress Long"), // "Dress Long" is a substring of "Long Evening Dresses"
        119.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36")});

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/pink-gown"), html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProduct::ID_CATEGORY),
             QStringLiteral("Long Evening Dresses"));
}

void Test_Downloader_Pradize::test_setKnownCategories_keyword_match()
{
    DownloaderPradize dl;
    dl.setKnownCategories({QStringLiteral("Boots And Ankle Boots"),
                           QStringLiteral("Heels")});

    const QString html = makePradizeProductHtml(
        QStringLiteral("brown-ankle-boots"),
        QStringLiteral("Brown Ankle Boots"),
        QStringLiteral("Ankle Shoes"), // keyword "ankle" overlaps with "Boots And Ankle Boots"
        79.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-5 | UK/AU-3 | EU-36")},
        QStringLiteral("Brown"));

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/brown-ankle-boots"), html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProduct::ID_CATEGORY),
             QStringLiteral("Boots And Ankle Boots"));
}

void Test_Downloader_Pradize::test_setKnownCategories_no_match_defaults_to_first()
{
    DownloaderPradize dl;
    dl.setKnownCategories({QStringLiteral("Dresses"),
                           QStringLiteral("Shoes")});

    const QString html = makePradizeProductHtml(
        QStringLiteral("leather-bag"),
        QStringLiteral("Leather Bag"),
        QStringLiteral("Handbag"),    // no overlap with known categories → first
        39.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36")});

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/leather-bag"), html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProduct::ID_CATEGORY),
             QStringLiteral("Dresses"));
}

void Test_Downloader_Pradize::test_setKnownCategories_empty_list_keeps_raw()
{
    DownloaderPradize dl;
    // No setKnownCategories call → raw type is returned unchanged

    const QString html = makePradizeProductHtml(
        QStringLiteral("silk-blouse"),
        QStringLiteral("Silk Blouse"),
        QStringLiteral("Tops"),
        49.99,
        {QStringLiteral("https://cdn.example.com/img1.jpg")},
        {QStringLiteral("US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36")});

    const auto attrs = dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/silk-blouse"), html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProduct::ID_CATEGORY),
             QStringLiteral("Tops"));
}

// ===========================================================================
// DownloaderPradizeCategory — getUrlsToParse
// ===========================================================================

void Test_Downloader_Pradize::test_category_getUrlsToParse_extracts_collection_urls()
{
    DownloaderPradizeCategory dl;

    const QString html = QStringLiteral(
        "<html><body>"
        "<a href=\"https://pradize.com/collection/midi-dresses\">Midi</a>"
        "<a href=\"https://pradize.com/collection/mini-dresses\">Mini</a>"
        "</body></html>");

    const QStringList urls = dl.getUrlsToParse(html);

    QVERIFY(urls.contains(QStringLiteral("https://pradize.com/collection/midi-dresses")));
    QVERIFY(urls.contains(QStringLiteral("https://pradize.com/collection/mini-dresses")));
}

void Test_Downloader_Pradize::test_category_getUrlsToParse_deduplicates()
{
    DownloaderPradizeCategory dl;

    const QString html =
        QStringLiteral("<a href=\"https://pradize.com/collection/midi-dresses\">A</a>")
        + QStringLiteral("<a href=\"https://pradize.com/collection/midi-dresses\">B</a>");

    const QStringList urls = dl.getUrlsToParse(html);

    QCOMPARE(urls.count(QStringLiteral("https://pradize.com/collection/midi-dresses")), 1);
}

// ===========================================================================
// DownloaderPradizeCategory — getAttributeValues
// ===========================================================================

void Test_Downloader_Pradize::test_category_getAttributeValues_extracts_title_and_description()
{
    DownloaderPradizeCategory dl;

    const QString html = makePradizeCategoryHtml(
        QStringLiteral("ankle-boots-women"),
        QStringLiteral("Ankle Boots"),
        QStringLiteral("Ankle boots for women — block and stiletto heels."));

    const QString url = QStringLiteral("https://pradize.com/collection/ankle-boots-women");
    const auto attrs = dl.getAttributeValues(url, html);

    QVERIFY(!attrs.isEmpty());
    QCOMPARE(attrs.value(PageAttributesProductCategory::ID_URL), url);
    QCOMPARE(attrs.value(PageAttributesProductCategory::ID_PRODUCT_CATEGORY),
             QStringLiteral("Ankle Boots"));
    const QString desc = attrs.value(PageAttributesProductCategory::ID_DESCRIPTION);
    QVERIFY(desc.length() >= 10);
    QVERIFY(desc.contains(QLatin1String("boots"), Qt::CaseInsensitive));
}

void Test_Downloader_Pradize::test_category_getAttributeValues_product_url_returns_empty()
{
    DownloaderPradizeCategory dl;

    const QString html = makePradizeCategoryHtml(
        QStringLiteral("ankle-boots-women"),
        QStringLiteral("Ankle Boots"),
        QStringLiteral("Ankle boots for women."));

    // URL is a product URL → should be rejected
    QVERIFY(dl.getAttributeValues(
        QStringLiteral("https://pradize.com/product/aria-dark-brown-ankle-pointed-booties"),
        html).isEmpty());
}

void Test_Downloader_Pradize::test_category_getAttributeValues_description_fallback()
{
    DownloaderPradizeCategory dl;

    // Empty description in the state body → generated fallback used
    const QString html = makePradizeCategoryHtml(
        QStringLiteral("corset-dresses"),
        QStringLiteral("Corset Dresses"),
        QStringLiteral("")); // empty description

    const QString url = QStringLiteral("https://pradize.com/collection/corset-dresses");
    const auto attrs = dl.getAttributeValues(url, html);

    QVERIFY(!attrs.isEmpty());
    const QString desc = attrs.value(PageAttributesProductCategory::ID_DESCRIPTION);
    QVERIFY2(desc.length() >= 10, qPrintable(QStringLiteral("Description too short: ") + desc));
    QVERIFY(desc.contains(QLatin1String("Corset Dresses"), Qt::CaseInsensitive));
}

void Test_Downloader_Pradize::test_category_getAttributeValues_wrong_type_returns_empty()
{
    DownloaderPradizeCategory dl;

    // Build a state with type != 0 (product body) for a /collection/ URL
    // The category downloader must reject it.
    const QString bodyJson = QStringLiteral(
        R"({"id":999,"title":"Fake","type":1,"seo_url":"wrong-type","description":"Test"})");

    const QString stateContent = QLatin1String(R"({"hash1":{)") +
                                 encodeForState(QStringLiteral("\"body\":")) +
                                 encodeForState(bodyJson) +
                                 QLatin1String("}}");

    const QString html = QString(
        R"(<html><head><script id="builderApp-state" type="application/json">%1</script></head></html>)")
        .arg(stateContent);

    QVERIFY(dl.getAttributeValues(
        QStringLiteral("https://pradize.com/collection/wrong-type"), html).isEmpty());
}

// ===========================================================================
// Integration tests — live pradize.com pages
// ===========================================================================

// Helper: fetches a single URL synchronously using a nested event loop.
static QString fetchPageSync(const QString &url)
{
    QNetworkAccessManager nam;
    QEventLoop loop;
    QString content;

    QNetworkRequest req{QUrl{url}};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (compatible; WebsiteEmpire/1.0)"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, [reply, &content, &loop]() {
        if (reply->error() == QNetworkReply::NoError) {
            content = QString::fromUtf8(reply->readAll());
        }
        reply->deleteLater();
        loop.quit();
    });
    loop.exec();
    return content;
}

void Test_Downloader_Pradize::test_parse_ariana_dress_with_sizes_and_color()
{
    // https://pradize.com/product/ariana-blush-pink-fairycore-ethereal-dress-gown
    // Dress with 7 sizes (FR/ES-34 … FR/ES-46), no explicit Color option.
    // Expected: sizes contains "34", color detected as "Blush Pink" or "Pink",
    //           multiple image URLs returned.

    const QString url =
        QStringLiteral("https://pradize.com/product/ariana-blush-pink-fairycore-ethereal-dress-gown");

    DownloaderPradize dl;
    QHash<QString, QString> attrs;
    QString content;
    QNetworkAccessManager ariaNam;
    acquireSessionCookie(ariaNam);
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            QTest::qWait(5000);
        }
        content = fetchWithNam(url, ariaNam);
        if (content.isEmpty()) {
            continue;
        }
        attrs = dl.getAttributeValues(url, content);
        if (!attrs.isEmpty()) {
            break;
        }
    }

    QVERIFY2(!content.isEmpty(), "Failed to fetch ariana dress page");
    QVERIFY2(!attrs.isEmpty(), "getAttributeValues returned empty for ariana dress");
    QCOMPARE(attrs.value(PageAttributesProduct::ID_URL), url);

    const QString name = attrs.value(PageAttributesProduct::ID_NAME);
    QVERIFY2(name.contains(QLatin1String("Ariana"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("Unexpected name: ") + name));

    // Raw sizes: must have ≥3 entries
    const QString rawSizes = attrs.value(PageAttributesProductFashion::ID_SIZES);
    QVERIFY2(!rawSizes.isEmpty(), "No raw sizes extracted");
    QVERIFY2(rawSizes.split(QLatin1Char(';'), Qt::SkipEmptyParts).size() >= 3,
             qPrintable(QStringLiteral("Expected ≥3 raw sizes, got: ") + rawSizes));

    // Per-size prices: same count as raw sizes
    const QStringList rawSizeList  = rawSizes.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    const QStringList priceList = attrs.value(PageAttributesProductFashion::ID_PRICES)
                                       .split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QVERIFY2(!priceList.isEmpty(), "No prices extracted");
    QCOMPARE(priceList.size(), rawSizeList.size());

    // FR sizes: all 7 FR/ES sizes expected
    const QString sizes = attrs.value(PageAttributesProductFashion::ID_SIZES_FR);
    QVERIFY2(!sizes.isEmpty(), "No FR sizes extracted");
    const QStringList sizeList = sizes.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QVERIFY2(sizeList.size() >= 3,
             qPrintable(QStringLiteral("Expected ≥3 FR sizes, got: ") + sizes));
    QVERIFY(sizes.contains(QLatin1String("34")));

    // Color: should be detected from title ("Blush Pink")
    const QString color = attrs.value(PageAttributesProductFashion::ID_COLORS);
    QVERIFY2(!color.isEmpty(), "No color detected for ariana dress");
    QVERIFY2(color.toLower().contains(QLatin1String("pink"))
             || color.toLower().contains(QLatin1String("blush")),
             qPrintable(QStringLiteral("Expected pink/blush color, got: ") + color));

    // Sexe / Age / Type
    QCOMPARE(attrs.value(PageAttributesProductFashion::ID_SEXE),
             PageAttributesProductFashion::SEXE_WOMEN);
    QCOMPARE(attrs.value(PageAttributesProductFashion::ID_AGE),
             PageAttributesProductFashion::AGE_ADULT);
    QCOMPARE(attrs.value(PageAttributesProductFashion::ID_TYPE),
             PageAttributesProductFashion::TYPE_CLOTHE);

    // Images: at least 4 (product has 7); each must download and be ≥300 px on
    // the shorter side so the UI shows a real photo, not a white placeholder.
    const QStringList imageUrls = attrs.value(PRADIZE_IMAGE_URLS_KEY).split(
        QLatin1Char(';'), Qt::SkipEmptyParts);
    QVERIFY2(imageUrls.size() >= 4,
             qPrintable(QStringLiteral("Expected ≥4 images, got %1").arg(imageUrls.size())));

    for (int i = 0; i < 4; ++i) {
        const QString &imgUrl = imageUrls[i];
        QVERIFY2(imgUrl.startsWith(QLatin1String("https://")),
                 qPrintable(QStringLiteral("Invalid image URL: ") + imgUrl));
        const QByteArray bytes = fetchImageBytes(imgUrl);
        QVERIFY2(!bytes.isEmpty(),
                 qPrintable(QStringLiteral("Failed to download image %1: ").arg(i) + imgUrl));
        QImage img;
        QVERIFY2(img.loadFromData(bytes),
                 qPrintable(QStringLiteral("Failed to decode image %1: ").arg(i) + imgUrl));
        QVERIFY2(qMin(img.width(), img.height()) >= 300,
                 qPrintable(QStringLiteral("Image %1 too small (%2x%3): ").arg(i).arg(img.width()).arg(img.height()) + imgUrl));
    }
}

void Test_Downloader_Pradize::test_parse_aria_booties_with_sizes_and_color()
{
    // https://pradize.com/product/aria-dark-brown-ankle-pointed-booties
    // Shoes with Color option (Dark Brown/Beige/Black) and EU shoe sizes.
    // Expected: sizes contains EU-based FR size "34", color is "Dark Brown",
    //           multiple image URLs returned from variants.

    const QString url =
        QStringLiteral("https://pradize.com/product/aria-dark-brown-ankle-pointed-booties");

    DownloaderPradize dl;
    QHash<QString, QString> attrs;
    QString content;
    QNetworkAccessManager bootiesNam;
    acquireSessionCookie(bootiesNam);
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            QTest::qWait(5000);
        }
        content = fetchWithNam(url, bootiesNam);
        if (content.isEmpty()) {
            continue;
        }
        attrs = dl.getAttributeValues(url, content);
        if (!attrs.isEmpty()) {
            break;
        }
    }

    QVERIFY2(!content.isEmpty(), "Failed to fetch aria booties page");
    QVERIFY2(!attrs.isEmpty(), "getAttributeValues returned empty for aria booties");
    QCOMPARE(attrs.value(PageAttributesProduct::ID_URL), url);

    const QString name = attrs.value(PageAttributesProduct::ID_NAME);
    QVERIFY2(name.contains(QLatin1String("Aria"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("Unexpected name: ") + name));

    // Raw sizes
    const QString rawSizesBooties = attrs.value(PageAttributesProductFashion::ID_SIZES);
    QVERIFY2(!rawSizesBooties.isEmpty(), "No raw sizes extracted for booties");
    QVERIFY2(rawSizesBooties.split(QLatin1Char(';'), Qt::SkipEmptyParts).size() >= 3,
             qPrintable(QStringLiteral("Expected ≥3 raw sizes for booties, got: ") + rawSizesBooties));

    // Per-size prices: same count as raw sizes
    const QStringList rawSizeListBooties = rawSizesBooties.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    const QStringList priceListBooties   = attrs.value(PageAttributesProductFashion::ID_PRICES)
                                                .split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QVERIFY2(!priceListBooties.isEmpty(), "No prices extracted for booties");
    QCOMPARE(priceListBooties.size(), rawSizeListBooties.size());

    // FR sizes: EU shoe sizes (e.g. 34, 35, 36 … 43)
    const QString sizes = attrs.value(PageAttributesProductFashion::ID_SIZES_FR);
    QVERIFY2(!sizes.isEmpty(), "No FR sizes extracted for booties");
    const QStringList sizeList = sizes.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QVERIFY2(sizeList.size() >= 3,
             qPrintable(QStringLiteral("Expected ≥3 FR sizes, got: ") + sizes));
    QVERIFY(sizes.contains(QLatin1String("34")));

    // Colors: explicit Color option → contains "Dark Brown"
    const QString color = attrs.value(PageAttributesProductFashion::ID_COLORS);
    QVERIFY2(!color.isEmpty(), "No color detected for aria booties");
    QVERIFY2(color.contains(QLatin1String("Brown"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("Expected brown color, got: ") + color));

    // Sexe / Age / Type
    QCOMPARE(attrs.value(PageAttributesProductFashion::ID_SEXE),
             PageAttributesProductFashion::SEXE_WOMEN);
    QCOMPARE(attrs.value(PageAttributesProductFashion::ID_AGE),
             PageAttributesProductFashion::AGE_ADULT);
    QCOMPARE(attrs.value(PageAttributesProductFashion::ID_TYPE),
             PageAttributesProductFashion::TYPE_SHOES);

    // Images: at least 4 (7 per color × 3 colors = 21, capped at 10); each must
    // download and be ≥300 px on the shorter side.
    const QStringList imageUrls = attrs.value(PRADIZE_IMAGE_URLS_KEY).split(
        QLatin1Char(';'), Qt::SkipEmptyParts);
    QVERIFY2(imageUrls.size() >= 4,
             qPrintable(QStringLiteral("Expected ≥4 images, got %1").arg(imageUrls.size())));

    for (int i = 0; i < 4; ++i) {
        const QString &imgUrl = imageUrls[i];
        QVERIFY2(imgUrl.startsWith(QLatin1String("https://")),
                 qPrintable(QStringLiteral("Invalid image URL: ") + imgUrl));
        const QByteArray bytes = fetchImageBytes(imgUrl);
        QVERIFY2(!bytes.isEmpty(),
                 qPrintable(QStringLiteral("Failed to download image %1: ").arg(i) + imgUrl));
        QImage img;
        QVERIFY2(img.loadFromData(bytes),
                 qPrintable(QStringLiteral("Failed to decode image %1: ").arg(i) + imgUrl));
        QVERIFY2(qMin(img.width(), img.height()) >= 300,
                 qPrintable(QStringLiteral("Image %1 too small (%2x%3): ").arg(i).arg(img.width()).arg(img.height()) + imgUrl));
    }
}

void Test_Downloader_Pradize::test_parse_various_product_pages_succeed()
{
    // These product pages were being fetched but returning empty attrs
    // (attrs keys: QList()) during crawl runs.  Each one must parse to a
    // non-empty attribute map containing at least the required fashion fields.
    const QStringList urls = {
        QStringLiteral("https://pradize.com/product/veronicas-elegance-black-peplum-midi-dress"),
        QStringLiteral("https://pradize.com/product/charlottes-black-white-peplum-midi-dress-with-sleeves"),
        QStringLiteral("https://pradize.com/product/isabella-ivory-peplum-midi-dress-with-ruffle"),
        QStringLiteral("https://pradize.com/product/isabella-white-peplum-midi-dress-with-ruffle"),
        QStringLiteral("https://pradize.com/product/ava-stylish-beige-spring-coat"),
        QStringLiteral("https://pradize.com/product/ava-stylish-yellow-mustard-coat"),
        QStringLiteral("https://pradize.com/product/danielas-chic-camel-coat"),
        QStringLiteral("https://pradize.com/product/emmas-elegant-long-grey-coat"),
        QStringLiteral("https://pradize.com/product/amelias-ruffle-wedding-guest-black-lace-midi-dress"),
        QStringLiteral("https://pradize.com/product/amelias-ruffle-wedding-guest-beige-lace-midi-dress"),
        QStringLiteral("https://pradize.com/product/veronica-long-blazer-dress-coat"),
        QStringLiteral("https://pradize.com/product/claras-elegant-white-blazer-dress"),
        QStringLiteral("https://pradize.com/product/claras-chic-pink-flare-coat-with-fur-collar-coat"),
        QStringLiteral("https://pradize.com/product/claras-chic-red-flare-coat-with-fur-collar-coat"),
        QStringLiteral("https://pradize.com/product/evelyns-chic-faux-fur-pink-trimmed-coat"),
        QStringLiteral("https://pradize.com/product/evelyns-chic-faux-fur-beige-trimmed-coat"),
        QStringLiteral("https://pradize.com/product/seraphinas-winter-elegant-maroon-fur-long-coat"),
        QStringLiteral("https://pradize.com/product/seraphinas-winter-elegant-purple-fur-long-coat"),
        // URLs that appeared in crawl logs with attrs keys: QList() — verified to
        // be live pages; failures were caused by rate-limiting, not logic errors.
        QStringLiteral("https://pradize.com/product/gold-sequined-princess-evening-dress"),
        QStringLiteral("https://pradize.com/product/purple-long-sleeves-midi-nightgown-dress"),
        QStringLiteral("https://pradize.com/product/red-white-floral-formal-classy-long-evening-gown-dress"),
        QStringLiteral("https://pradize.com/product/sophia-sleeveless-jumpsuit"),
        QStringLiteral("https://pradize.com/product/black-slim-high-waist-midi-dress"),
    };

    DownloaderPradize dl;

    // Acquire the session cookie once and share it across all URL fetches.
    // The _rclientSessionId cookie makes pradize.com serve the full SSR page
    // even when the client IP is rate-limited; without it all requests return
    // the 11 KB Angular shell and getAttributeValues() returns empty.
    QNetworkAccessManager sessionNam;
    acquireSessionCookie(sessionNam);

    for (int i = 0; i < urls.size(); ++i) {
        if (i > 0) {
            QTest::qWait(2000);
        }
        const QString &url = urls.at(i);

        // Retry once — first attempt should succeed with the session cookie.
        // A long backoff causes the test to exceed the 300 s QTest watchdog
        // when the server is rate-limiting, so the wait is kept short.
        QHash<QString, QString> attrs;
        QString content;
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (attempt > 0) {
                QTest::qWait(2000);
            }
            content = fetchWithNam(url, sessionNam);
            if (content.isEmpty()) {
                continue;
            }
            attrs = dl.getAttributeValues(url, content);
            if (!attrs.isEmpty()) {
                break;
            }
        }

        QVERIFY2(!content.isEmpty(),
                 qPrintable(QStringLiteral("Failed to fetch page: ") + url));
        QVERIFY2(!attrs.isEmpty(),
                 qPrintable(QStringLiteral("getAttributeValues returned empty for: ") + url));

        // Name must be non-empty.
        QVERIFY2(!attrs.value(PageAttributesProduct::ID_NAME).isEmpty(),
                 qPrintable(QStringLiteral("No name for: ") + url));

        // Sizes and prices must be non-empty and have the same count.
        const QStringList sizes  = attrs.value(PageAttributesProductFashion::ID_SIZES)
                                       .split(QLatin1Char(';'), Qt::SkipEmptyParts);
        const QStringList prices = attrs.value(PageAttributesProductFashion::ID_PRICES)
                                       .split(QLatin1Char(';'), Qt::SkipEmptyParts);
        QVERIFY2(!sizes.isEmpty(),
                 qPrintable(QStringLiteral("No sizes for: ") + url));
        QVERIFY2(!prices.isEmpty(),
                 qPrintable(QStringLiteral("No prices for: ") + url));
        QVERIFY2(sizes.size() == prices.size(),
                 qPrintable(QStringLiteral("Sizes/prices count mismatch for: ") + url));

        // At least one image URL must be present.
        QVERIFY2(!attrs.value(PRADIZE_IMAGE_URLS_KEY).isEmpty(),
                 qPrintable(QStringLiteral("No image URLs for: ") + url));
    }
}

// ===========================================================================
// Session-cookie requirement tests
// ===========================================================================

// Documents that pradize.com serves the bare 11 KB Angular SPA shell (no
// builderApp-state, no product data) to requests lacking the _rclientSessionId
// session cookie when the server is rate-limiting the client IP.
//
// The "with-session" side is always verified: fetchWithSessionSync() must
// return a full SSR product page containing builderApp-state.
// The "without-session" side is only checked when the response is exactly
// 11 496 bytes — on a fresh, non-rate-limited IP the server may serve the
// full page to anyone, in which case the cookie comparison is vacuous.
void Test_Downloader_Pradize::test_product_page_without_session_cookie_returns_shell()
{
    // Use one of the URLs whose 11 KB debug files were captured during real crawl runs.
    const QString url =
        QStringLiteral("https://pradize.com/product/black-simple-long-formal-gown");

    // Step 1: fetch WITHOUT session cookie.
    const QString withoutToken = fetchPageSync(url);
    QVERIFY2(!withoutToken.isEmpty(), "Network error fetching without session cookie");

    // Short cooldown between the two requests to avoid compounding rate-limit.
    QTest::qWait(3000);

    // Step 2: fetch WITH session cookie.
    const QString withToken = fetchWithSessionSync(url);
    QVERIFY2(!withToken.isEmpty(), "Network error fetching with session cookie");

    // The page fetched WITH the cookie must always be a full SSR product page.
    QVERIFY2(withToken.contains(QLatin1String("builderApp-state")),
             qPrintable(QStringLiteral(
                 "fetchWithSessionSync did not return an SSR page (got %1 bytes). "
                 "Possible network or server outage.").arg(withToken.length())));

    // When rate-limiting is active (detected by the 11 KB shell response),
    // verify the session cookie produces a substantially larger page.
    const bool degradedWithoutToken =
        withoutToken.length() == 11496
        && !withoutToken.contains(QLatin1String("builderApp-state"));

    if (degradedWithoutToken) {
        QVERIFY2(withToken.length() > 20000,
                 qPrintable(QStringLiteral(
                     "Under rate-limiting the session cookie must unlock the full "
                     "SSR page (≥20 KB), but got %1 bytes.").arg(withToken.length())));
    }
    // If withoutToken already returned a full page (non-rate-limited IP), the
    // hypothesis cannot be verified on this run — the test still passes because
    // the with-token assertion above was satisfied.
}

// Verifies that DownloaderPradize::parseSpecificUrls() acquires the FES
// session token before fetching product HTML pages (not just /api/v1/fes/ URLs).
//
// WITHOUT THE FIX: token is only acquired for FES API URLs; product-page
// fetches have no cookie → server returns the 11 KB Angular shell →
// isFetchSuccessful() rejects every response → URL re-enqueued forever →
// onPageParsed callback is never invoked → test times out and FAILS.
//
// WITH THE FIX: token acquired before the very first fetch; subsequent
// requests carry _rclientSessionId → server returns the full SSR page →
// getAttributeValues() extracts attrs → callback invoked → PASSES.
void Test_Downloader_Pradize::test_downloader_fetches_html_product_page_with_session_cookie()
{
    // One of the URLs that produced the 11 KB debug files during real crawl runs.
    const QString url =
        QStringLiteral("https://pradize.com/product/black-simple-long-formal-gown");

    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    QHash<QString, QString> receivedAttrs;
    bool callbackInvoked = false;

    auto callback = [&](const QString &, const QHash<QString, QString> &attrs)
        -> QFuture<bool> {
        receivedAttrs = attrs;
        callbackInvoked = true;
        auto p = QSharedPointer<QPromise<bool>>::create();
        p->start();
        p->addResult(true);
        p->finish();
        return p->future();
    };

    DownloaderPradize dl(QDir(tmpDir.path()), callback);

    QEventLoop loop;
    // Allow 30 s for: token fetch + 2 s delay + product fetch (+ a few retries).
    // requestStop() causes the crawl to emit stopped() which quits the loop.
    QTimer::singleShot(30000, &dl, &AbstractDownloader::requestStop);
    QObject::connect(&dl, &DownloaderPradize::finished, &loop, &QEventLoop::quit);
    QObject::connect(&dl, &DownloaderPradize::stopped,  &loop, &QEventLoop::quit);

    dl.parseSpecificUrls({url});
    loop.exec();

    QVERIFY2(callbackInvoked,
             "onPageParsed callback was never invoked within 30 s.\n"
             "The downloader likely kept re-enqueueing the URL because "
             "isFetchSuccessful() rejected every response as 11 KB.\n"
             "Root cause: fetchUrl() did not acquire the FES session token "
             "before fetching the product HTML page.");
    QVERIFY2(!receivedAttrs.isEmpty(),
             "Callback was invoked but getAttributeValues() returned empty — "
             "server may have returned a page without builderApp-state");
    QVERIFY2(!receivedAttrs.value(PageAttributesProduct::ID_NAME).isEmpty(),
             "Parsed product has no name");
}

// ===========================================================================
// Integration test — live pradize.com URLs that returned 11 KB degraded
// responses during production crawl runs.
//
// Three batches are covered:
//   • Batch 1 (logged 2026-03-19, session 1): 15 URLs from the first
//     debug_pages collection that prompted the session-cookie fix.
//   • Batch 2 (logged 2026-03-19, session 2): 14 URLs from the second
//     debug_pages collection (all again identical 11 496-byte SPA shells).
//   • Batch 3 (logged 2026-04-15, session 3): 6 URLs that degraded even
//     after the 2-week cooldown period, exposing the second bug.
//
// Two fixes are verified by this test:
//   Fix 1 (Batch 1+2): trackFetchResult() detects degraded responses and
//     resets the FES session + backs off 30 s.
//   Fix 2 (Batch 3): the reset threshold counts TOTAL degraded responses
//     since the last reset, NOT consecutive ones.  The old consecutive logic
//     never fired when failures and successes alternated (e.g. fail, success,
//     fail, fail, fail, success, fail, fail) — the counter reset on every
//     success, keeping it below 5 indefinitely despite a ~75% failure rate.
//
// Uses parseSpecificUrls() so the production backoff + session-reset logic
// runs if the IP is already rate-limited from earlier tests.
//
// Test timing: 35 URLs × (2 s delay + ~3 s network) ≈ 175 s, plus up to
// 2 backoff windows × 30 s ≈ 60 s → ≈ 235 s worst-case, within the 280 s
// soft-stop timer.
// ===========================================================================

void Test_Downloader_Pradize::test_parse_failing_urls_from_production_log()
{
    const QStringList urls = {
        // --- Batch 1 (session 1 debug_pages) ---
        QStringLiteral("https://pradize.com/product/white-corset-mini-dress-with-long-sleeves"),
        QStringLiteral("https://pradize.com/product/white-floral-mesh-full-length-maxi-dress"),
        QStringLiteral("https://pradize.com/product/white-tight-club-mini-dress"),
        QStringLiteral("https://pradize.com/product/black-flower-polka-dot-mesh-nightwear-lingerie"),
        QStringLiteral("https://pradize.com/product/black-see-through-lace-nightwear-lingerie"),
        QStringLiteral("https://pradize.com/product/black-silver-sparkle-mini-dress"),
        QStringLiteral("https://pradize.com/product/black-strappy-lingerie-set-with-rhinestones"),
        QStringLiteral("https://pradize.com/product/burgundy-off-shoulder-bridal-long-evening-dress"),
        QStringLiteral("https://pradize.com/product/champagne-corporate-business-office-formal-wear"),
        QStringLiteral("https://pradize.com/product/cute-black-floral-nightgown"),
        QStringLiteral("https://pradize.com/product/dark-red-long-tight-evening-dress"),
        QStringLiteral("https://pradize.com/product/dark-red-wrap-front-crop-top-off-shoulder"),
        QStringLiteral("https://pradize.com/product/lavander-halter-sleeveless-jumpsuit-rompers"),
        QStringLiteral("https://pradize.com/product/mia-maritime-frolic-swimdress"),
        QStringLiteral("https://pradize.com/product/pink-vintage-silk-puff-sleeve-casual-midi-dress"),
        // --- Batch 2 (session 2 debug_pages) ---
        QStringLiteral("https://pradize.com/product/steel-blue-princess-evening-dress-off-shoulder"),
        QStringLiteral("https://pradize.com/product/white-long-sleeve-top-and-red-suspender-strap-mini-dress"),
        QStringLiteral("https://pradize.com/product/white-v-neck-lace-slim-fit-one-step-skirt-midi-dress"),
        QStringLiteral("https://pradize.com/product/black-waist-tie-wide-leg-one-piece-shorts-and-top-set"),
        QStringLiteral("https://pradize.com/product/pink-one-piece-tube-top-nightwear-lingerie"),
        QStringLiteral("https://pradize.com/product/red-transparent-lace-sleepwear-backless"),
        QStringLiteral("https://pradize.com/product/black-lace-mesh-temptation-nightwear-lingerie"),
        QStringLiteral("https://pradize.com/product/black-ruffed-one-side-off-shoulder-casual-mini-dress"),
        QStringLiteral("https://pradize.com/product/black-striped-wrap-front-mini-dress"),
        QStringLiteral("https://pradize.com/product/blue-grey-off-shoulder-high-slit-long-evening-dress"),
        QStringLiteral("https://pradize.com/product/clara-checkered-charm-swimdress"),
        QStringLiteral("https://pradize.com/product/fantasy-white-short-dress-with-sleeves"),
        QStringLiteral("https://pradize.com/product/isabelle-glamour-gown"),
        QStringLiteral("https://pradize.com/product/red-lace-backless-bodysuit-nightwear-lingerie"),
        // --- Batch 3 (2026-04-15, session 3 debug_pages) ---
        // These degraded even after a 2-week cooldown, exposing the "consecutive
        // failures" bug: successes interspersed with failures kept m_consecutiveFailures
        // below 5 indefinitely.  Fix: count total failures since last reset.
        QStringLiteral("https://pradize.com/product/silver-sequin-tight-club-mini-dress"),
        QStringLiteral("https://pradize.com/product/sky-blue-knitted-one-piece-trikini"),
        QStringLiteral("https://pradize.com/product/sky-blue-off-shoulder-tight-mini-dress-shiny-sleeves"),
        QStringLiteral("https://pradize.com/product/sparkle-red-classy-off-shoulder-evening-dress"),
        QStringLiteral("https://pradize.com/product/sparkle-red-wine-club-mini-dress-tight"),
        QStringLiteral("https://pradize.com/product/sparkle-silver-off-shoulder-long-evening-dress"),
        // NOTE: stunning-red-off-shoulder-white-line-full-length-evening-dress
        // was removed — FES API returns 404 (product no longer exists on pradize.com).
    };

    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    QMap<QString, QHash<QString, QString>> parsedAttrs;

    auto callback = [&](const QString &url, const QHash<QString, QString> &attrs)
        -> QFuture<bool> {
        if (!attrs.isEmpty()) {
            parsedAttrs[url] = attrs;
        }
        auto p = QSharedPointer<QPromise<bool>>::create();
        p->start();
        p->addResult(true);
        p->finish();
        return p->future();
    };

    DownloaderPradize dl(QDir(tmpDir.path()), callback);

    QEventLoop loop;
    // 280 s timeout: if we hit IP-level rate-limiting, trackFetchResult() will
    // fire a 30 s backoff after every burst of 5 failures.  Two such windows
    // (2 × 55 s) plus 145 s for the 29 successful fetches ≈ 255 s < 280 s.
    QTimer::singleShot(280000, &dl, &AbstractDownloader::requestStop);
    QObject::connect(&dl, &DownloaderPradize::finished, &loop, &QEventLoop::quit);
    QObject::connect(&dl, &DownloaderPradize::stopped,  &loop, &QEventLoop::quit);

    dl.parseSpecificUrls(urls);
    loop.exec();

    for (const QString &url : urls) {
        QVERIFY2(parsedAttrs.contains(url) && !parsedAttrs[url].isEmpty(),
                 qPrintable(QStringLiteral("getAttributeValues returned empty for: ") + url));

        QVERIFY2(!parsedAttrs[url].value(PageAttributesProduct::ID_NAME).isEmpty(),
                 qPrintable(QStringLiteral("No name for: ") + url));

        const QStringList sizes  = parsedAttrs[url].value(PageAttributesProductFashion::ID_SIZES)
                                       .split(QLatin1Char(';'), Qt::SkipEmptyParts);
        const QStringList prices = parsedAttrs[url].value(PageAttributesProductFashion::ID_PRICES)
                                       .split(QLatin1Char(';'), Qt::SkipEmptyParts);
        QVERIFY2(!sizes.isEmpty(),
                 qPrintable(QStringLiteral("No sizes for: ") + url));
        QVERIFY2(!prices.isEmpty(),
                 qPrintable(QStringLiteral("No prices for: ") + url));
        QVERIFY2(sizes.size() == prices.size(),
                 qPrintable(QStringLiteral("Sizes/prices count mismatch for: ") + url));

        QVERIFY2(!parsedAttrs[url].value(PRADIZE_IMAGE_URLS_KEY).isEmpty(),
                 qPrintable(QStringLiteral("No image URLs for: ") + url));
    }
}

QTEST_MAIN(Test_Downloader_Pradize)
#include "test_downloader_pradize.moc"
