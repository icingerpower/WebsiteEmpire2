#include "DownloaderVogelvoerkopen.h"

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include "aspire/attributes/PageAttributesProduct.h"
#include "aspire/attributes/PageAttributesProductPetFood.h"

DECLARE_DOWNLOADER(DownloaderVogelvoerkopen)

AbstractDownloader *DownloaderVogelvoerkopen::createInstance(const QDir &workingDir) const
{
    return new DownloaderVogelvoerkopen(workingDir);
}

static const QString DOMAIN = QStringLiteral("vogelvoerkopen.nl");

DownloaderVogelvoerkopen::DownloaderVogelvoerkopen(const QDir &workingDir,
                                                   PageParsedCallback onPageParsed,
                                                   QObject *parent)
    : AbstractDownloader(workingDir, std::move(onPageParsed), parent)
{
}

QString DownloaderVogelvoerkopen::getId() const
{
    return QStringLiteral("vogelvoerkopen");
}

QString DownloaderVogelvoerkopen::getName() const
{
    return tr("Vogelvoerkopen.nl");
}

QString DownloaderVogelvoerkopen::getDescription() const
{
    return tr("Crawler for vogelvoerkopen.nl — a Dutch pet-food webshop (WordPress/WooCommerce).");
}

QStringList DownloaderVogelvoerkopen::getSeedUrls() const
{
    return {QStringLiteral("https://www.vogelvoerkopen.nl/product-sitemap1.xml")};
}

QString DownloaderVogelvoerkopen::getImageUrlAttributeKey() const
{
    return VOGELVOERKOPEN_IMAGE_URL_KEY;
}

AbstractPageAttributes *DownloaderVogelvoerkopen::createPageAttributes() const
{
    return new PageAttributesProductPetFood;
}

// ---------------------------------------------------------------------------
// URL extraction
// ---------------------------------------------------------------------------

QStringList DownloaderVogelvoerkopen::getUrlsToParse(const QString &content) const
{
    QStringList urls;
    QSet<QString> seen;

    auto add = [&](const QString &url) {
        if (!seen.contains(url)) {
            seen.insert(url);
            urls << url;
        }
    };

    // XML sitemap: extract <loc> elements
    if (content.contains(QLatin1String("<urlset")) || content.contains(QLatin1String("<sitemapindex"))) {
        static const QRegularExpression locRe(QStringLiteral("<loc>([^<]+)</loc>"));
        auto it = locRe.globalMatch(content);
        while (it.hasNext()) {
            const QString url = it.next().captured(1).trimmed();
            if (url.contains(DOMAIN)) {
                add(url);
            }
        }
        return urls;
    }

    // HTML: extract href attributes
    static const QRegularExpression hrefRe(
        QStringLiteral(R"(href\s*=\s*["'](https?://[^"'#?]+)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    auto it = hrefRe.globalMatch(content);
    while (it.hasNext()) {
        const QString url = it.next().captured(1).trimmed();
        if (url.contains(DOMAIN)) {
            add(url);
        }
    }

    return urls;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Converts a WooCommerce weight attribute string (e.g. "500-g", "1-kg", "1-5-kg")
// to grams.  Returns -1 if unparseable.
static int parseVariantWeightGrams(const QString &raw)
{
    QString s = raw.toLower().trimmed();
    s.replace(',', '.');

    // "1-5-kg"  → "1.5 kg"  (hyphen as decimal separator in slug)
    static const QRegularExpression slugDotRe(QStringLiteral(R"((\d+)-(\d+)-(kg|g|gram|kilogram))"));
    QRegularExpressionMatch m = slugDotRe.match(s);
    if (m.hasMatch()) {
        bool ok;
        const double d = QString("%1.%2").arg(m.captured(1), m.captured(2)).toDouble(&ok);
        const QString unit = m.captured(3);
        if (ok) {
            return unit.startsWith('k') ? qRound(d * 1000.0) : qRound(d);
        }
    }

    static const QRegularExpression kgRe(QStringLiteral(R"((\d+(?:\.\d+)?)\s*-?\s*(?:kg|kilogram))"));
    static const QRegularExpression gRe(QStringLiteral(R"((\d+)\s*-?\s*(?:gram|g)(?:[^a-z]|$))"));

    m = kgRe.match(s);
    if (m.hasMatch()) {
        bool ok;
        const double d = m.captured(1).toDouble(&ok);
        if (ok) {
            return qRound(d * 1000.0);
        }
    }

    m = gRe.match(s);
    if (m.hasMatch()) {
        bool ok;
        const int g = m.captured(1).toInt(&ok);
        if (ok) {
            return g;
        }
    }

    return -1;
}

// Tries to infer a weight in grams from a free-text product name.
// Returns -1 if no recognisable weight unit is found.
static int parseNameWeightGrams(const QString &name)
{
    const QString n = name.toLower();

    static const QRegularExpression kgRe(QStringLiteral(R"((\d+(?:[,.]\d+)?)\s*kg)"));
    static const QRegularExpression gRe(QStringLiteral(R"((\d+)\s*gr?(?:am)?\b)"));
    static const QRegularExpression lRe(QStringLiteral(R"((\d+(?:[,.]\d+)?)\s*l(?:iter)?\b)"));

    QRegularExpressionMatch m;

    m = kgRe.match(n);
    if (m.hasMatch()) {
        bool ok;
        QString val = m.captured(1);
        val.replace(',', '.');
        const double d = val.toDouble(&ok);
        if (ok) {
            return qRound(d * 1000.0);
        }
    }

    m = gRe.match(n);
    if (m.hasMatch()) {
        bool ok;
        const int g = m.captured(1).toInt(&ok);
        if (ok) {
            return g;
        }
    }

    m = lRe.match(n);
    if (m.hasMatch()) {
        bool ok;
        QString val = m.captured(1);
        val.replace(',', '.');
        const double d = val.toDouble(&ok);
        if (ok) {
            return qRound(d * 1000.0); // approximate: 1 L ≈ 1 kg
        }
    }

    return -1;
}

// Strips HTML tags and decodes common HTML entities.
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
    s = s.simplified();
    return s;
}

// Extracts a price (as a positive double string) from a QJsonValue that may be
// a scalar, an Offer object, or an AggregateOffer / array of Offer objects.
static QString extractPrice(const QJsonValue &offersVal)
{
    auto priceFromObj = [](const QJsonObject &obj) -> double {
        // Prefer "price" scalar; fall back to "lowPrice" for AggregateOffer.
        // WooCommerce sometimes emits price as a JSON string ("61.39") rather
        // than a JSON number, so try toString() as a fallback when toDouble()
        // returns 0.
        const QJsonValue priceVal = obj[QStringLiteral("price")];
        double p = priceVal.isDouble() ? priceVal.toDouble()
                                       : priceVal.toString().toDouble();
        if (p <= 0.0) {
            const QJsonValue lowPriceVal = obj[QStringLiteral("lowPrice")];
            p = lowPriceVal.isDouble() ? lowPriceVal.toDouble()
                                       : lowPriceVal.toString().toDouble();
        }
        // Yoast SEO WooCommerce format: price nested inside priceSpecification
        // array instead of a top-level "price" field.
        if (p <= 0.0) {
            const QJsonArray specArr = obj[QStringLiteral("priceSpecification")].toArray();
            for (const QJsonValue &spec : specArr) {
                if (!spec.isObject()) {
                    continue;
                }
                const QJsonValue specPrice = spec.toObject()[QStringLiteral("price")];
                const double sp = specPrice.isDouble() ? specPrice.toDouble()
                                                       : specPrice.toString().toDouble();
                if (sp > 0.0) {
                    p = sp;
                    break;
                }
            }
        }
        return p;
    };

    double price = 0.0;

    if (offersVal.isObject()) {
        price = priceFromObj(offersVal.toObject());
    } else if (offersVal.isArray()) {
        const QJsonArray arr = offersVal.toArray();
        for (const QJsonValue &v : arr) {
            if (v.isObject()) {
                const double p = priceFromObj(v.toObject());
                if (p > 0.0) {
                    price = p;
                    break;
                }
            }
        }
    }

    if (price > 0.0) {
        return QString::number(price, 'f', 2);
    }
    return QString{};
}

// ---------------------------------------------------------------------------
// Category matching
// ---------------------------------------------------------------------------

void DownloaderVogelvoerkopen::setKnownCategories(const QStringList &categories)
{
    m_knownCategories = categories;
}

QString DownloaderVogelvoerkopen::matchCategory(const QString &rawCategory) const
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

    // 2. Substring containment: raw is contained in known, or known is contained in raw.
    for (const QString &cat : std::as_const(m_knownCategories)) {
        const QString catNorm = cat.toLower().trimmed();
        if (rawNorm.contains(catNorm) || catNorm.contains(rawNorm)) {
            return cat;
        }
    }

    // 3. Keyword overlap scoring: split both strings by non-word characters and
    //    count how many tokens from the raw category appear in the known category.
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

    // 4. Default: return the first known category.
    return m_knownCategories.first();
}

// ---------------------------------------------------------------------------
// Attribute extraction
// ---------------------------------------------------------------------------

QHash<QString, QString> DownloaderVogelvoerkopen::getAttributeValues(const QString &url, const QString &content) const
{
    // Collect all JSON-LD blocks
    static const QRegularExpression ldRe(
        QStringLiteral(R"(<script[^>]+type\s*=\s*["']application/ld\+json["'][^>]*>([\s\S]*?)</script>)"),
        QRegularExpression::CaseInsensitiveOption);

    QJsonObject productObj;
    QJsonObject breadcrumbObj;

    // Recursively searches val for objects with the given @type, descending
    // into top-level arrays and Yoast SEO "@graph" wrappers.
    std::function<void(const QJsonValue &)> collectTypes = [&](const QJsonValue &val) {
        if (val.isArray()) {
            for (const QJsonValue &v : val.toArray()) {
                collectTypes(v);
            }
            return;
        }
        if (!val.isObject()) {
            return;
        }
        const QJsonObject obj = val.toObject();
        const QString type = obj[QStringLiteral("@type")].toString();
        if (type == QLatin1String("Product") && productObj.isEmpty()) {
            productObj = obj;
        } else if (type == QLatin1String("BreadcrumbList") && breadcrumbObj.isEmpty()) {
            breadcrumbObj = obj;
        }
        // Descend into @graph wrapper (Yoast SEO format).
        const QJsonValue graphVal = obj[QStringLiteral("@graph")];
        if (graphVal.isArray()) {
            collectTypes(graphVal);
        }
    };

    auto it = ldRe.globalMatch(content);
    while (it.hasNext()) {
        const QString jsonText = it.next().captured(1).trimmed();
        const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
        collectTypes(doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()));
    }

    if (productObj.isEmpty()) {
        return {}; // Not a product page
    }

    // --- Basic fields ---
    const QString name = productObj[QStringLiteral("name")].toString().trimmed();
    if (name.isEmpty()) {
        return {};
    }

    QString description = cleanHtml(productObj[QStringLiteral("description")].toString());
    if (description.isEmpty()) {
        description = name;
    }
    description = description.left(4000); // cap to avoid overly large DB values

    const QString salePrice = [&]() -> QString {
        const QString p = extractPrice(productObj[QStringLiteral("offers")]);
        return p.isEmpty() ? QStringLiteral("0.01") : p;
    }();

    // --- Image URL ---
    QString imageUrl;
    const QJsonValue imgVal = productObj[QStringLiteral("image")];
    if (imgVal.isString()) {
        imageUrl = imgVal.toString();
    } else if (imgVal.isArray()) {
        const QJsonArray arr = imgVal.toArray();
        if (!arr.isEmpty()) {
            imageUrl = arr.first().isString() ? arr.first().toString()
                                              : arr.first().toObject()[QStringLiteral("url")].toString();
        }
    } else if (imgVal.isObject()) {
        imageUrl = imgVal.toObject()[QStringLiteral("url")].toString();
    }

    // --- Category from BreadcrumbList ---
    // vogelvoerkopen.nl nests the name as item.item.name (Yoast SEO format);
    // simpler sites use item.name directly.
    QString category = tr("Vogelvoer");
    if (!breadcrumbObj.isEmpty()) {
        const QJsonArray items = breadcrumbObj[QStringLiteral("itemListElement")].toArray();
        // Walk backwards; skip the product itself (last entry) and "Home"
        for (int i = items.size() - 2; i >= 0; --i) {
            const QJsonObject item = items[i].toObject();
            const QString direct = item[QStringLiteral("name")].toString().trimmed();
            const QString nested = item[QStringLiteral("item")].toObject()
                                       [QStringLiteral("name")].toString().trimmed();
            const QString itemName = direct.isEmpty() ? nested : direct;
            if (!itemName.isEmpty() && itemName.compare(QLatin1String("Home"), Qt::CaseInsensitive) != 0) {
                category = itemName;
                break;
            }
        }
    }

    // --- Weight + prices from WooCommerce variations ---
    QString weightGrStr;
    QString pricesStr;

    static const QRegularExpression varRe(
        QStringLiteral(R"xxx(data-product_variations\s*=\s*"([^"]+)")xxx"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch varMatch = varRe.match(content);
    if (varMatch.hasMatch()) {
        QString varJson = varMatch.captured(1);
        varJson.replace(QLatin1String("&quot;"), QLatin1String("\""));
        varJson.replace(QLatin1String("&amp;"),  QLatin1String("&"));
        varJson.replace(QLatin1String("&#039;"), QLatin1String("'"));

        const QJsonDocument varDoc = QJsonDocument::fromJson(varJson.toUtf8());
        if (varDoc.isArray()) {
            QStringList weights;
            QStringList prices;
            for (const QJsonValue &v : varDoc.array()) {
                if (!v.isObject()) {
                    continue;
                }
                const QJsonObject varObj = v.toObject();
                if (!varObj[QStringLiteral("is_in_stock")].toBool(true)) {
                    continue;
                }

                const QJsonObject attrs = varObj[QStringLiteral("attributes")].toObject();
                int weightG = -1;
                for (const QString &key : attrs.keys()) {
                    if (key.contains(QLatin1String("gewicht"))
                        || key.contains(QLatin1String("weight"))
                        || key.contains(QLatin1String("inhoud"))
                        || key.contains(QLatin1String("volume"))
                        || key.contains(QLatin1String("maat"))
                        || key.contains(QLatin1String("size")))
                    {
                        weightG = parseVariantWeightGrams(attrs[key].toString());
                        if (weightG >= 0) {
                            break;
                        }
                    }
                }
                if (weightG < 0) {
                    continue;
                }

                const double price = varObj[QStringLiteral("display_price")].toDouble();
                if (price <= 0.0) {
                    continue;
                }

                weights << QString::number(weightG);
                prices << QString::number(price, 'f', 2);
            }
            if (!weights.isEmpty()) {
                weightGrStr = weights.join(';');
                pricesStr = prices.join(';');
            }
        }
    }

    // Fallback for simple (non-variable) products: parse weight from name
    if (weightGrStr.isEmpty()) {
        int wg = parseNameWeightGrams(name);
        if (wg < 0) {
            wg = 1; // unknown weight — record as 1 (0 fails some downstream checks)
        }
        weightGrStr = QString::number(wg);
        pricesStr = salePrice;
    }

    QHash<QString, QString> result;
    result[PageAttributesProduct::ID_URL]              = url;
    result[PageAttributesProduct::ID_NAME]             = name;
    result[PageAttributesProduct::ID_DESCRIPTION]      = description;
    result[PageAttributesProduct::ID_SALE_PRICE]       = salePrice;
    result[PageAttributesProduct::ID_CATEGORY]         = matchCategory(category);
    result[PageAttributesProductPetFood::ID_WEIGHT_GR] = weightGrStr;
    result[PageAttributesProductPetFood::ID_PRICES]    = pricesStr;
    result[VOGELVOERKOPEN_IMAGE_URL_KEY]                = imageUrl;
    return result;
}

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------

QFuture<QString> DownloaderVogelvoerkopen::fetchUrl(const QString &url)
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
    }

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
