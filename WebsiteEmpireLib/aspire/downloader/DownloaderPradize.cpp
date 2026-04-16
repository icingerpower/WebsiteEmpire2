#include "DownloaderPradize.h"

#include <functional>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include "aspire/attributes/PageAttributesProduct.h"
#include "aspire/attributes/PageAttributesProductFashion.h"

DECLARE_DOWNLOADER(DownloaderPradize)

static const QString PRADIZE_DOMAIN = QStringLiteral("pradize.com");

// Known colour keywords, ordered longest-first so compound colours like
// "Blush Pink" are matched before their single-word components.
static const QStringList s_colorKeywords = {
    QStringLiteral("rose gold"),
    QStringLiteral("light pink"),
    QStringLiteral("blush pink"),
    QStringLiteral("baby pink"),
    QStringLiteral("dusty pink"),
    QStringLiteral("hot pink"),
    QStringLiteral("dark brown"),
    QStringLiteral("light brown"),
    QStringLiteral("chocolate brown"),
    QStringLiteral("dark green"),
    QStringLiteral("light green"),
    QStringLiteral("forest green"),
    QStringLiteral("sage green"),
    QStringLiteral("olive green"),
    QStringLiteral("emerald green"),
    QStringLiteral("mint green"),
    QStringLiteral("dark blue"),
    QStringLiteral("light blue"),
    QStringLiteral("navy blue"),
    QStringLiteral("royal blue"),
    QStringLiteral("sky blue"),
    QStringLiteral("baby blue"),
    QStringLiteral("dark red"),
    QStringLiteral("wine red"),
    QStringLiteral("dark grey"),
    QStringLiteral("light grey"),
    QStringLiteral("charcoal grey"),
    QStringLiteral("dark gray"),
    QStringLiteral("light gray"),
    QStringLiteral("charcoal gray"),
    QStringLiteral("pale yellow"),
    QStringLiteral("white"),
    QStringLiteral("black"),
    QStringLiteral("red"),
    QStringLiteral("blue"),
    QStringLiteral("green"),
    QStringLiteral("yellow"),
    QStringLiteral("orange"),
    QStringLiteral("purple"),
    QStringLiteral("pink"),
    QStringLiteral("brown"),
    QStringLiteral("grey"),
    QStringLiteral("gray"),
    QStringLiteral("beige"),
    QStringLiteral("ivory"),
    QStringLiteral("cream"),
    QStringLiteral("nude"),
    QStringLiteral("navy"),
    QStringLiteral("burgundy"),
    QStringLiteral("teal"),
    QStringLiteral("coral"),
    QStringLiteral("gold"),
    QStringLiteral("silver"),
    QStringLiteral("rose"),
    QStringLiteral("blush"),
    QStringLiteral("lavender"),
    QStringLiteral("mint"),
    QStringLiteral("champagne"),
    QStringLiteral("camel"),
    QStringLiteral("tan"),
    QStringLiteral("charcoal"),
    QStringLiteral("turquoise"),
    QStringLiteral("wine"),
    QStringLiteral("olive"),
    QStringLiteral("khaki"),
    QStringLiteral("amber"),
    QStringLiteral("mustard"),
    QStringLiteral("indigo"),
    QStringLiteral("lilac"),
    QStringLiteral("fuchsia"),
    QStringLiteral("plum"),
    QStringLiteral("maroon"),
    QStringLiteral("chocolate"),
    QStringLiteral("cognac"),
    QStringLiteral("rust"),
    QStringLiteral("mocha"),
    QStringLiteral("aqua"),
    QStringLiteral("taupe"),
};

DownloaderPradize::DownloaderPradize(const QDir &workingDir,
                                     PageParsedCallback onPageParsed,
                                     QObject *parent)
    : AbstractDownloader(workingDir, std::move(onPageParsed), parent)
{
}

AbstractDownloader *DownloaderPradize::createInstance(const QDir &workingDir) const
{
    return new DownloaderPradize(workingDir);
}

QString DownloaderPradize::getId() const
{
    return QStringLiteral("pradize");
}

QString DownloaderPradize::getName() const
{
    return tr("Pradize.com");
}

QString DownloaderPradize::getDescription() const
{
    return tr("Crawler for product pages on pradize.com — a fashion e-commerce store.");
}

bool DownloaderPradize::supportsFileUrlDownload() const
{
    return true;
}

bool DownloaderPradize::isFetchSuccessful(const QString &url, const QString &content) const
{
    // 404 sentinel: the product was removed; mark visited, do not retry.
    if (content == PRADIZE_404_SENTINEL) {
        return true;
    }
    // FES API JSON responses are small by design — always valid.
    if (url.contains(QLatin1String("/api/v1/fes/"))) {
        return !content.isEmpty();
    }
    // HTML product and collection pages are typically 100–300 KB.
    // The rate-limit error page returned by pradize.com is exactly 11 496 bytes;
    // anything under 20 KB is a degraded/blocked response and should be retried.
    return content.length() >= 20000;
}

QStringList DownloaderPradize::getSeedUrls() const
{
    return {
        QStringLiteral("https://pradize.com/collection/ethereal-dresses-fancy-flowy-gowns"),
        QStringLiteral("https://pradize.com/collection/classy-evening-maxi-dresses-long-formal"),
        QStringLiteral("https://pradize.com/collection/glitter-sequins-shimmery-sparkle-evening-dresses"),
        QStringLiteral("https://pradize.com/collection/midi-dresses"),
        QStringLiteral("https://pradize.com/collection/mini-dresses"),
        QStringLiteral("https://pradize.com/collection/summer-dresses"),
        QStringLiteral("https://pradize.com/collection/workwear-dresses"),
        QStringLiteral("https://pradize.com/collection/corset-dresses"),
        QStringLiteral("https://pradize.com/collection/floral-dresses"),
        QStringLiteral("https://pradize.com/collection/heels-pumps-stiletto-block"),
        QStringLiteral("https://pradize.com/collection/ankle-boots-women"),
        QStringLiteral("https://pradize.com/collection/sexy-sleepwear-nightgowns-pajamas-nighties"),
        QStringLiteral("https://pradize.com/collection/all-jumpsuits"),
        QStringLiteral("https://pradize.com/collection/ladies-tops-and-blouses"),
        QStringLiteral("https://pradize.com/collection/skirts"),
    };
}

QString DownloaderPradize::getImageUrlAttributeKey() const
{
    return PRADIZE_IMAGE_URLS_KEY;
}

AbstractPageAttributes *DownloaderPradize::createPageAttributes() const
{
    return new PageAttributesProductFashion;
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

// Extracts and decodes the builderApp-state JSON object from a page.
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

// Strips HTML tags and decodes common HTML entities from a description string.
static QString cleanHtml(const QString &html)
{
    QString s = html;
    s.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
    s.replace(QLatin1String("&amp;"),  QLatin1String("&"));
    s.replace(QLatin1String("&lt;"),   QLatin1String("<"));
    s.replace(QLatin1String("&gt;"),   QLatin1String(">"));
    s.replace(QLatin1String("&quot;"), QLatin1String("\""));
    s.replace(QLatin1String("&#039;"), QLatin1String("'"));
    s.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    return s.simplified();
}

// Extracts the FR (or equivalent EU) size number from a size option value.
// Supports two formats:
//   "US-2 | UK/AU-6 | EU/DE-32 | FR/ES-34"  →  "34"  (clothing: explicit FR)
//   "US-3 | UK/AU-1 | EU-34"                →  "34"  (shoes: EU == FR)
static QString extractFrSize(const QString &sizeValue)
{
    // Explicit FR/ES size (clothing).
    static const QRegularExpression frRe(QStringLiteral(R"(FR/ES-(\d+(?:\.\d+)?))"));
    QRegularExpressionMatch m = frRe.match(sizeValue);
    if (m.hasMatch()) {
        return m.captured(1);
    }

    // EU-only size (shoes: EU numbering == French numbering).
    // Use word boundary \b before EU to avoid matching "EU/DE-" which is a
    // different (German/continental) sizing system.
    static const QRegularExpression euRe(QStringLiteral(R"(\bEU-(\d+(?:\.\d+)?))"));
    m = euRe.match(sizeValue);
    if (m.hasMatch()) {
        return m.captured(1);
    }

    return {};
}

// Searches text (lowercased) for any colour keyword.
// Returns the colour as it appears in the original (mixed-case) text, or
// an empty string when nothing is found.
static QString findColorInText(const QString &text)
{
    const QString lower = text.toLower();
    for (const QString &kw : std::as_const(s_colorKeywords)) {
        const int pos = lower.indexOf(kw);
        if (pos >= 0) {
            return text.mid(pos, kw.length());
        }
    }
    return {};
}

// Derives the TYPE_* constant from the raw pradize product type string.
static QString deriveProductType(const QString &rawCategory)
{
    const QString lower = rawCategory.toLower();
    if (lower.contains(QLatin1String("shoe"))
        || lower.contains(QLatin1String("boot"))
        || lower.contains(QLatin1String("heel"))
        || lower.contains(QLatin1String("sandal"))
        || lower.contains(QLatin1String("pump"))
        || lower.contains(QLatin1String("stiletto"))
        || lower.contains(QLatin1String("platform"))
        || lower.contains(QLatin1String("mule"))
        || lower.contains(QLatin1String("wedge"))
        || lower.contains(QLatin1String("sneaker"))
        || lower.contains(QLatin1String("loafer"))
        || lower.contains(QLatin1String("flat")))
    {
        return PageAttributesProductFashion::TYPE_SHOES;
    }
    if (lower.contains(QLatin1String("bag"))
        || lower.contains(QLatin1String("purse"))
        || lower.contains(QLatin1String("accessory"))
        || lower.contains(QLatin1String("jewel"))
        || lower.contains(QLatin1String("belt"))
        || lower.contains(QLatin1String("hat"))
        || lower.contains(QLatin1String("scarf"))
        || lower.contains(QLatin1String("wallet")))
    {
        return PageAttributesProductFashion::TYPE_ACCESSORY;
    }
    return PageAttributesProductFashion::TYPE_CLOTHE;
}

// Extracts material composition patterns (e.g. "80% polyester") from text.
// Returns a space-joined string of all matches, or an empty string.
static QString extractMaterial(const QString &text)
{
    static const QRegularExpression materialRe(
        QStringLiteral(R"(\d+%\s*[A-Za-z]+)"),
        QRegularExpression::CaseInsensitiveOption);
    QStringList parts;
    auto it = materialRe.globalMatch(text);
    while (it.hasNext()) {
        parts << it.next().captured(0).simplified();
        if (parts.size() >= 5) {
            break;
        }
    }
    return parts.join(QLatin1Char(' '));
}

// ---------------------------------------------------------------------------
// JSON-LD fallback (Angular SPA version of the page, no builderApp-state)
// ---------------------------------------------------------------------------

// Parses a product page that carries Schema.org JSON-LD but no builderApp-state.
// pradize.com serves this lighter Angular SPA page to non-SSR user agents.
// Returns partial attributes: name, price, description, category, colour.
// Sizes default to "One Size" — the SPA page has no variant-level data.
// Image URLs are absent — the SPA loads them dynamically via Angular.
// Returns an empty map when no Product JSON-LD block is found.
static QHash<QString, QString> getAttributeValuesFromJsonLd(
    const QString &url, const QString &content,
    const std::function<QString(const QString &)> &matchCategoryFn)
{
    static const QRegularExpression ldRe(
        QStringLiteral(R"(<script[^>]+type\s*=\s*"application/ld\+json"[^>]*>([\s\S]*?)</script>)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = ldRe.match(content);
    if (!m.hasMatch()) {
        return {};
    }

    const QJsonDocument doc = QJsonDocument::fromJson(m.captured(1).toUtf8());
    if (!doc.isObject()) {
        return {};
    }
    const QJsonObject obj = doc.object();
    if (obj[QStringLiteral("@type")].toString() != QLatin1String("Product")) {
        return {};
    }

    const QString name = obj[QStringLiteral("name")].toString().trimmed();
    if (name.isEmpty()) {
        return {};
    }

    const QJsonObject offers = obj[QStringLiteral("offers")].toObject();
    const double price = offers[QStringLiteral("price")].toVariant().toDouble();
    const QString priceStr = price > 0.0
                           ? QString::number(price, 'f', 2)
                           : QStringLiteral("0.01");

    const QString description = obj[QStringLiteral("description")].toString().trimmed();

    // Category: prefer the gtag view_item event payload which embeds "category":"Dress Long".
    // The pattern contains a literal quote before the closing paren so a named raw-string
    // delimiter is needed to avoid early termination of the raw literal.
    QString rawCategory;
    static const QRegularExpression gtagCatRe(
        QString("\"category\"\\s*:\\s*\"([^\"]+)\""));
    const QRegularExpressionMatch catMatch = gtagCatRe.match(content);
    if (catMatch.hasMatch()) {
        rawCategory = catMatch.captured(1);
    }
    const QString category = matchCategoryFn(rawCategory.isEmpty() ? name : rawCategory);

    QString color = findColorInText(name);
    if (color.isEmpty()) {
        color = findColorInText(description);
    }

    QHash<QString, QString> result;
    result[PageAttributesProduct::ID_URL]              = url;
    result[PageAttributesProduct::ID_NAME]             = name;
    result[PageAttributesProduct::ID_DESCRIPTION]      = description.isEmpty() ? name : description;
    result[PageAttributesProduct::ID_SALE_PRICE]       = priceStr;
    result[PageAttributesProduct::ID_CATEGORY]         = category;
    result[PageAttributesProductFashion::ID_SIZES]     = QStringLiteral("One Size");
    result[PageAttributesProductFashion::ID_PRICES]    = priceStr;
    result[PageAttributesProductFashion::ID_SIZES_FR]  = QString{};
    result[PageAttributesProductFashion::ID_SEXE]      = PageAttributesProductFashion::SEXE_WOMEN;
    result[PageAttributesProductFashion::ID_AGE]       = PageAttributesProductFashion::AGE_ADULT;
    result[PageAttributesProductFashion::ID_TYPE]      =
        deriveProductType(rawCategory.isEmpty() ? name : rawCategory);
    if (!color.isEmpty()) {
        result[PageAttributesProductFashion::ID_COLORS] = color;
    }

    // Extract og:image from <head> — present on the full SSR page and can also
    // appear on Angular SPA pages served by some CDN edge nodes.
    static const QRegularExpression ogImageRe(
        QStringLiteral(R"x(<meta[^>]+property\s*=\s*"og:image"[^>]+content\s*=\s*"([^"]+)")x"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch imgMatch = ogImageRe.match(content);
    if (imgMatch.hasMatch()) {
        result[PRADIZE_IMAGE_URLS_KEY] = imgMatch.captured(1).trimmed();
    }

    return result;
}

// ---------------------------------------------------------------------------
// Category matching
// ---------------------------------------------------------------------------

void DownloaderPradize::setKnownCategories(const QStringList &categories)
{
    m_knownCategories = categories;
}

QString DownloaderPradize::matchCategory(const QString &rawCategory) const
{
    if (m_knownCategories.isEmpty()) {
        return rawCategory;
    }

    const QString rawNorm = rawCategory.toLower().trimmed();

    // 1. Exact match (case-insensitive).
    for (const QString &cat : std::as_const(m_knownCategories)) {
        if (cat.toLower().trimmed() == rawNorm) {
            return cat;
        }
    }

    // 2. Substring containment.
    for (const QString &cat : std::as_const(m_knownCategories)) {
        const QString catNorm = cat.toLower().trimmed();
        if (rawNorm.contains(catNorm) || catNorm.contains(rawNorm)) {
            return cat;
        }
    }

    // 3. Keyword overlap scoring.
    auto tokenize = [](const QString &s) -> QStringList {
        return s.toLower().split(QRegularExpression(QStringLiteral(R"(\W+)")),
                                 Qt::SkipEmptyParts);
    };

    const QStringList rawTokens = tokenize(rawCategory);
    int bestScore = 0;
    int bestIdx   = 0;
    for (int i = 0; i < m_knownCategories.size(); ++i) {
        const QStringList knownTokens = tokenize(m_knownCategories[i]);
        int score = 0;
        for (const QString &t : rawTokens) {
            if (knownTokens.contains(t)) {
                ++score;
            }
        }
        if (score > bestScore) {
            bestScore = score;
            bestIdx   = i;
        }
    }
    if (bestScore > 0) {
        return m_knownCategories[bestIdx];
    }

    // 4. Default: first known category.
    return m_knownCategories.first();
}

// ---------------------------------------------------------------------------
// URL extraction
// ---------------------------------------------------------------------------

// Parses a CommerceHQ FES products API JSON response and returns:
//   • One https://pradize.com/product/<seo_url> URL per item
//   • The _links.next.href pagination URL when present
static QStringList getUrlsFromProductsApiJson(const QString &content)
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

    // Extract product HTML page URLs from items[].seo_url.
    const QJsonArray items = obj[QStringLiteral("items")].toArray();
    for (const QJsonValue &item : items) {
        if (!item.isObject()) {
            continue;
        }
        const QString seoUrl = item.toObject()[QStringLiteral("seo_url")].toString().trimmed();
        if (!seoUrl.isEmpty()) {
            add(QStringLiteral("https://pradize.com/product/") + seoUrl);
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

QStringList DownloaderPradize::getUrlsToParse(const QString &content) const
{
    // FES API JSON response: extract product URLs + pagination.
    if (content.trimmed().startsWith(QLatin1Char('{'))) {
        return getUrlsFromProductsApiJson(content);
    }

    // HTML page: extract product and collection hrefs; for every collection slug
    // also enqueue the FES API products URL so all products are discovered.
    QStringList urls;
    QSet<QString> seen;

    auto add = [&](const QString &url) {
        if (!seen.contains(url)) {
            seen.insert(url);
            urls << url;
        }
    };

    const QString decoded = decodeCommerceHqEntities(content);

    // Relative /product/<slug> paths
    static const QRegularExpression prodRe(
        QStringLiteral(R"(/product/([a-z0-9][a-z0-9-]*)(?=[^a-z0-9-]|$))"));
    auto it = prodRe.globalMatch(decoded);
    while (it.hasNext()) {
        add(QStringLiteral("https://") + PRADIZE_DOMAIN + it.next().captured(0));
    }

    // Relative /collection/<slug> paths — also enqueue the API products URL.
    static const QRegularExpression collRe(
        QStringLiteral(R"(/collection/([a-z0-9][a-z0-9-]*)(?=[^a-z0-9-]|$))"));
    auto it2 = collRe.globalMatch(decoded);
    while (it2.hasNext()) {
        const QRegularExpressionMatch m = it2.next();
        add(QStringLiteral("https://") + PRADIZE_DOMAIN + m.captured(0));
        add(QStringLiteral("https://pradize.com/api/v1/fes/products?collection=")
            + m.captured(1) + QStringLiteral("&page=1&limit=100"));
    }

    // Full https://pradize.com/product/<slug> and /collection/<slug> URLs.
    static const QRegularExpression fullRe(
        QStringLiteral(R"(https?://(?:www\.)?pradize\.com/(product|collection)/([a-z0-9][a-z0-9-]*))"));
    auto it3 = fullRe.globalMatch(decoded);
    while (it3.hasNext()) {
        const QRegularExpressionMatch match = it3.next();
        const QString type = match.captured(1);
        const QString slug = match.captured(2);
        add(QStringLiteral("https://pradize.com/") + type + QLatin1Char('/') + slug);
        if (type == QLatin1String("collection")) {
            add(QStringLiteral("https://pradize.com/api/v1/fes/products?collection=")
                + slug + QStringLiteral("&page=1&limit=100"));
        }
    }

    return urls;
}

// ---------------------------------------------------------------------------
// Attribute extraction
// ---------------------------------------------------------------------------

QHash<QString, QString> DownloaderPradize::getAttributeValues(const QString &url,
                                                               const QString &content) const
{
    // Only process product pages.
    if (!url.contains(QLatin1String("/product/"))) {
        return {};
    }

    // Extract slug from URL.
    static const QRegularExpression slugRe(QStringLiteral(R"(/product/([^/?#]+))"));
    const QRegularExpressionMatch slugMatch = slugRe.match(url);
    if (!slugMatch.hasMatch()) {
        return {};
    }
    const QString urlSlug = slugMatch.captured(1);

    const QJsonObject stateObj = extractStateJson(content);
    if (stateObj.isEmpty()) {
        qDebug() << "DownloaderPradize: no builderApp-state JSON found for" << url
                 << "— content length:" << content.length()
                 << "(short = rate-limited / error page; long = Angular SPA without SSR)";
        // Short pages (<20 KB) are rate-limit error pages with no product data —
        // return empty so the caller can retry.
        // Larger pages are the Angular SPA version of the product page; try to
        // extract partial data from the Schema.org JSON-LD block instead.
        if (content.length() < 20000) {
            return {};
        }
        const auto matchFn = [this](const QString &raw) { return matchCategory(raw); };
        const QHash<QString, QString> jsonLdResult =
            getAttributeValuesFromJsonLd(url, content, matchFn);
        if (!jsonLdResult.isEmpty()) {
            const bool hasImage = jsonLdResult.contains(PRADIZE_IMAGE_URLS_KEY)
                                  && !jsonLdResult[PRADIZE_IMAGE_URLS_KEY].isEmpty();
            qDebug() << "DownloaderPradize: used JSON-LD fallback for" << url
                     << (hasImage ? "(partial data: no sizes; has og:image)"
                                  : "(partial data: no sizes or images)");
        } else {
            qDebug() << "DownloaderPradize: JSON-LD fallback also failed for" << url;
        }
        return jsonLdResult;
    }

    // Locate the product body in the state JSON.
    QJsonObject productBody;
    QStringList foundSeoUrls;
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
        const QString bodySeoUrl = body[QStringLiteral("seo_url")].toString();
        if (!bodySeoUrl.isEmpty()) {
            foundSeoUrls << bodySeoUrl;
        }
        if (bodySeoUrl == urlSlug && body.contains(QStringLiteral("title"))) {
            productBody = body;
            break;
        }
    }

    if (productBody.isEmpty()) {
        qDebug() << "DownloaderPradize: no product body matched slug" << urlSlug
                 << "— seo_urls found in state:" << foundSeoUrls;
        return {};
    }

    // --- Basic fields ---
    const QString name = productBody[QStringLiteral("title")].toString().trimmed();
    if (name.isEmpty()) {
        return {};
    }

    const QString rawCategory = productBody[QStringLiteral("type")].toString().trimmed();
    const QString category = rawCategory.isEmpty()
                           ? tr("Dress Long")
                           : matchCategory(rawCategory);

    const double priceDouble = productBody[QStringLiteral("default_price")].toDouble();
    const QString salePrice = priceDouble > 0.0
                            ? QString::number(priceDouble, 'f', 2)
                            : QStringLiteral("0.01");

    // --- Description ---
    QString description;
    const QJsonArray textareas = productBody[QStringLiteral("textareas")].toArray();
    for (const QJsonValue &tv : textareas) {
        if (!tv.isObject()) {
            continue;
        }
        const QString text = tv.toObject()[QStringLiteral("text")].toString();
        if (!text.isEmpty()) {
            description = cleanHtml(text).left(4000);
            break;
        }
    }
    if (description.isEmpty()) {
        description = name;
    }

    // --- Images ---
    // Primary: top-level "images" array.  Fallback: collect unique images from
    // the "variants" array (products where per-variant images are used instead).
    QStringList imageUrls;
    QSet<int> seenImageIds;

    auto addImages = [&](const QJsonArray &imgArr) {
        for (const QJsonValue &iv : imgArr) {
            if (!iv.isObject()) {
                continue;
            }
            const QJsonObject imgObj = iv.toObject();
            const int id = imgObj[QStringLiteral("id")].toInt();
            const QString path = imgObj[QStringLiteral("path")].toString();
            if (!path.isEmpty() && !seenImageIds.contains(id)) {
                seenImageIds.insert(id);
                imageUrls << path;
                if (imageUrls.size() >= MAX_IMAGES) {
                    return;
                }
            }
        }
    };

    const QJsonArray topImages = productBody[QStringLiteral("images")].toArray();
    addImages(topImages);

    if (imageUrls.isEmpty()) {
        const QJsonArray variants = productBody[QStringLiteral("variants")].toArray();
        for (const QJsonValue &vv : variants) {
            if (!vv.isObject()) {
                continue;
            }
            addImages(vv.toObject()[QStringLiteral("images")].toArray());
            if (imageUrls.size() >= MAX_IMAGES) {
                break;
            }
        }
    }

    // Also try default_image as last resort.
    if (imageUrls.isEmpty()) {
        const QJsonObject defImg = productBody[QStringLiteral("default_image")].toObject();
        const QString path = defImg[QStringLiteral("path")].toString();
        if (!path.isEmpty()) {
            imageUrls << path;
        }
    }

    // --- Options: sizes and colours ---
    QStringList rawSizes;
    QStringList frSizes;
    QStringList colors;

    const QJsonArray options = productBody[QStringLiteral("options")].toArray();
    for (const QJsonValue &ov : options) {
        if (!ov.isObject()) {
            continue;
        }
        const QJsonObject opt = ov.toObject();
        const QString optTitle = opt[QStringLiteral("title")].toString();
        const QJsonArray values = opt[QStringLiteral("values")].toArray();

        if (optTitle.compare(QLatin1String("Size"), Qt::CaseInsensitive) == 0) {
            for (const QJsonValue &sv : values) {
                const QString sizeVal = sv.toString().trimmed();
                if (!sizeVal.isEmpty()) {
                    rawSizes << sizeVal;
                    const QString fr = extractFrSize(sizeVal);
                    if (!fr.isEmpty()) {
                        frSizes << fr;
                    }
                }
            }
        } else if (optTitle.compare(QLatin1String("Color"),  Qt::CaseInsensitive) == 0
                   || optTitle.compare(QLatin1String("Colour"), Qt::CaseInsensitive) == 0)
        {
            for (const QJsonValue &cv : values) {
                const QString c = cv.toString().trimmed();
                if (!c.isEmpty()) {
                    colors << c;
                }
            }
        }
    }

    // Fallback: detect colour from product title then description.
    if (colors.isEmpty()) {
        const QString titleColor = findColorInText(name);
        if (!titleColor.isEmpty()) {
            colors << titleColor;
        }
    }
    if (colors.isEmpty()) {
        const QString descColor = findColorInText(description);
        if (!descColor.isEmpty()) {
            colors << descColor;
        }
    }

    // --- Per-size prices from variants ---
    // Build a map: size label → price so each raw size gets its variant's price.
    QHash<QString, double> sizePriceMap;
    const QJsonArray variants = productBody[QStringLiteral("variants")].toArray();
    for (const QJsonValue &vv : variants) {
        if (!vv.isObject()) {
            continue;
        }
        const QJsonObject v = vv.toObject();
        const double varPrice = v[QStringLiteral("price")].toDouble();
        if (varPrice <= 0.0) {
            continue;
        }
        const QJsonArray variantLabels = v[QStringLiteral("variant")].toArray();
        for (const QJsonValue &lv : variantLabels) {
            const QString label = lv.toString().trimmed();
            if (!label.isEmpty() && !sizePriceMap.contains(label)) {
                sizePriceMap.insert(label, varPrice);
            }
        }
    }

    // One price entry per size; fall back to default_price when no variant found.
    QStringList prices;
    if (rawSizes.isEmpty()) {
        // No Size option found — use a single "One Size" entry.
        rawSizes << QStringLiteral("One Size");
        prices   << QString::number(priceDouble > 0.0 ? priceDouble : 0.01, 'f', 2);
    } else {
        for (const QString &sizeVal : std::as_const(rawSizes)) {
            const double p = sizePriceMap.value(sizeVal, priceDouble);
            prices << QString::number(p > 0.0 ? p : priceDouble, 'f', 2);
        }
    }

    // --- Product type and material ---
    const QString productType = deriveProductType(rawCategory);
    const QString material    = extractMaterial(description);

    QHash<QString, QString> result;
    result[PageAttributesProduct::ID_URL]              = url;
    result[PageAttributesProduct::ID_NAME]             = name;
    result[PageAttributesProduct::ID_DESCRIPTION]      = description;
    result[PageAttributesProduct::ID_SALE_PRICE]       = salePrice;
    result[PageAttributesProduct::ID_CATEGORY]         = category;
    result[PageAttributesProductFashion::ID_SIZES]     = rawSizes.join(QLatin1Char(';'));
    result[PageAttributesProductFashion::ID_PRICES]    = prices.join(QLatin1Char(';'));
    result[PageAttributesProductFashion::ID_SIZES_FR]  = frSizes.join(QLatin1Char(';'));
    result[PageAttributesProductFashion::ID_COLORS]    = colors.join(QLatin1Char(';'));
    result[PageAttributesProductFashion::ID_SEXE]      = PageAttributesProductFashion::SEXE_WOMEN;
    result[PageAttributesProductFashion::ID_AGE]       = PageAttributesProductFashion::AGE_ADULT;
    result[PageAttributesProductFashion::ID_TYPE]      = productType;
    if (!material.isEmpty()) {
        result[PageAttributesProductFashion::ID_MATERIAL] = material;
    }
    result[PRADIZE_IMAGE_URLS_KEY]                     = imageUrls.join(QLatin1Char(';'));
    return result;
}

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------

QFuture<QString> DownloaderPradize::doFetch(const QString &url)
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
        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // A 404 means the product/collection no longer exists.  Return the
        // sentinel so isFetchSuccessful() marks the URL visited instead of
        // re-enqueuing it indefinitely (the server returns the same 11 KB
        // Angular SPA shell for 404s and for rate-limited requests).
        if (httpStatus == 404) {
            qDebug() << "DownloaderPradize: 404 for" << reply->url().toString()
                     << "— marking visited (product removed)";
            promise->addResult(PRADIZE_404_SENTINEL);
        } else {
            promise->addResult(QString::fromUtf8(reply->readAll()));
        }
        promise->finish();
        reply->deleteLater();
    });

    return promise->future();
}

void DownloaderPradize::trackFetchResult(const QString &url, const QString &content)
{
    if (isFetchSuccessful(url, content)) {
        // Successes do NOT reset m_degradedCount.  The old approach reset the
        // counter on every success, which prevented the session-reset trigger
        // from firing when failures and successes alternated (the pattern seen
        // in production: fail, success, fail, fail, fail, success, fail, fail …
        // kept m_consecutiveFailures below 5 indefinitely despite a ~75% failure
        // rate).  Counting total failures since the last reset is more robust.
        return;
    }

    ++m_degradedCount;

    // After 5 total degraded responses since the last session reset:
    //   1. Reset the FES session so a fresh _rclientSessionId is acquired
    //      on the next fetch — a new session may carry a fresh rate budget.
    //   2. Schedule a 30-second backoff so the server's rate-limit counter
    //      has time to reset before the next network request goes out.
    // fetchUrl() reads m_backoffUntilMs and waits out any remaining delay.
    if (m_degradedCount >= 5) {
        qDebug() << "DownloaderPradize: rate-limit detected after"
                 << m_degradedCount
                 << "degraded responses — resetting session and backing off 30 s";
        m_sessionAcquired = false;
        m_degradedCount = 0;
        m_backoffUntilMs = QDateTime::currentMSecsSinceEpoch() + 30000;
    }
}

QFuture<QString> DownloaderPradize::fetchUrl(const QString &url)
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
    }

    // When sustained rate-limiting has been detected, pause before making the
    // next request so the server's rate-limit counter can reset.
    // Only the FIRST fetchUrl() call after the backoff is set actually waits;
    // m_backoffUntilMs is cleared immediately so subsequent calls (for the
    // next queued URLs) see it as already expired and proceed normally.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_backoffUntilMs > nowMs) {
        const qint64 waitMs = m_backoffUntilMs - nowMs;
        m_backoffUntilMs = 0; // cleared before timer fires — no re-entry
        qDebug() << "DownloaderPradize: rate-limit backoff —"
                 << waitMs / 1000 << "s before next request";
        auto promise = QSharedPointer<QPromise<QString>>::create();
        promise->start();
        // Cap at 5 minutes to avoid silent hangs during tests.
        QTimer::singleShot(static_cast<int>(qMin(waitMs, qint64(300000))),
                           this, [this, url, promise]() {
            // fetchUrl() is safe to call here: m_backoffUntilMs is 0 and
            // m_sessionAcquired was reset by trackFetchResult(), so a fresh
            // session token will be acquired before the actual page fetch.
            fetchUrl(url).then(this, [promise](const QString &content) {
                promise->addResult(content);
                promise->finish();
            }).onFailed(this, [promise](const QException &) {
                promise->addResult(QString{});
                promise->finish();
            });
        });
        return promise->future();
    }

    // The _rclientSessionId cookie (set by the FES token endpoint) is required
    // for ALL requests to pradize.com — without it the server returns the bare
    // Angular SPA shell (11 KB) instead of the SSR product/collection page.
    // Acquired lazily before the very first request; also re-acquired after
    // sustained rate-limiting is detected (m_sessionAcquired reset above).
    if (!m_sessionAcquired) {
        m_sessionAcquired = true;
        const QString tokenUrl =
            QStringLiteral("https://pradize.com/api/v1/fes/token");
        return doFetch(tokenUrl).then(this, [this, url](const QString &) -> QFuture<QString> {
            return doFetch(url).then(this, [this, url](const QString &content) -> QString {
                trackFetchResult(url, content);
                return content;
            });
        }).unwrap();
    }

    return doFetch(url).then(this, [this, url](const QString &content) -> QString {
        trackFetchResult(url, content);
        return content;
    });
}
