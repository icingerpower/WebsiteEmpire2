#include "PageTypeArticle.h"

#include "website/AbstractEngine.h"
#include "website/ImageWriter.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/PageBlocCategory.h"
#include "website/social/AbstractSocialMedia.h"
#include "website/theme/AbstractTheme.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QImage>
#include <QLocale>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QSvgRenderer>

// =============================================================================
// Constructor / Destructor
// =============================================================================

PageTypeArticle::PageTypeArticle(CategoryTable &categoryTable)
    : m_categoryBloc(new PageBlocCategory(categoryTable))
    , m_categoryLinksBloc(categoryTable)
{
    m_blocs.append(m_categoryBloc.data()); // 0
    m_blocs.append(&m_textBloc);           // 1
    m_blocs.append(&m_socialTextBloc);     // 2 — social text metadata, first pass
    m_blocs.append(&m_autoLinkBloc);       // 3
    m_blocs.append(&m_categoryLinksBloc);  // 4
    m_blocs.append(&m_socialBloc);         // 5 — social image variants, second pass
    m_blocs.append(&m_metaBloc);           // 6 — SEO title + meta description
}

PageTypeArticle::~PageTypeArticle() = default;

// =============================================================================
// Accessors
// =============================================================================

QString PageTypeArticle::getTypeId()      const { return QLatin1String(TYPE_ID); }
QString PageTypeArticle::getDisplayName() const { return QLatin1String(DISPLAY_NAME); }

const QList<const AbstractPageBloc *> &PageTypeArticle::getPageBlocs() const
{
    return m_blocs;
}

void PageTypeArticle::setPageUrl(const QString &url)
{
    m_autoLinkBloc.setPageUrl(url);
}

const PageBlocSocial      &PageTypeArticle::socialTextBloc() const { return m_socialTextBloc; }
const PageBlocSocialMedia &PageTypeArticle::socialBloc()     const { return m_socialBloc; }
const PageBlocAutoLink    &PageTypeArticle::autoLinkBloc()   const { return m_autoLinkBloc; }
const PageBlocMeta        &PageTypeArticle::metaBloc()       const { return m_metaBloc; }

// =============================================================================
// prepareJsonLdImage
// =============================================================================

void PageTypeArticle::prepareJsonLdImage(const QDir &workingDir, const QString &domain)
{
    // Any second-pass social image works — no fallback needed.
    if (!m_socialBloc.imgOg().isEmpty() || !m_socialBloc.imgWide().isEmpty()
            || !m_socialBloc.imgSquare().isEmpty() || !m_socialBloc.imgPortrait().isEmpty()) {
        return;
    }

    // Extract the first IMGFIX SVG filename from the article text.
    static const QRegularExpression s_svgRe(
        QStringLiteral("\\[IMGFIX\\b[^\\]]*fileName=\"([^\"]+\\.svg)\""),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = s_svgRe.match(m_textBloc.text());
    if (!match.hasMatch()) {
        return;
    }
    const QString &svgFilename = match.captured(1);

    // Derive a stable fallback filename; stored under domain="" (global).
    const QString stem = svgFilename.left(svgFilename.lastIndexOf(QLatin1Char('.')));
    const QString fallbackFilename = stem + QStringLiteral("-og-auto.webp");

    ImageWriter writer(workingDir);

    // Skip if already cached from a previous generation run.
    if (writer.hasName(QString(), fallbackFilename)) {
        m_jsonLdFallbackImage = fallbackFilename;
        return;
    }

    // Fetch the source SVG: try global (domain="") first, then the site domain.
    // SVGs written by older launchers may be stored under the bare domain name.
    QByteArray svgData = writer.readSvg(QString(), svgFilename);
    if (svgData.isEmpty() && !domain.isEmpty()) {
        svgData = writer.readSvg(domain, svgFilename);
    }
    if (svgData.isEmpty()) {
        return;
    }

    // Rasterize to 1200×630 (OG landscape dimensions) on a white background.
    QSvgRenderer renderer(svgData);
    if (!renderer.isValid()) {
        return;
    }
    QImage img(1200, 630, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter painter(&img);
    renderer.render(&painter);
    painter.end();

    // Write WebP to images.db under domain="" (global fallback).
    if (writer.writeQImage(img, QString(), fallbackFilename) > 0) {
        m_jsonLdFallbackImage = fallbackFilename;
    }
}

// =============================================================================
// buildHeadMetaTags
// =============================================================================

namespace {

// Maps a two-letter language code to an og:locale value.  Falls back to
// uppercasing the code as the country component when the code is unknown.
QString langToLocale(const QString &lang)
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("en"), QStringLiteral("en_US")},
        {QStringLiteral("fr"), QStringLiteral("fr_FR")},
        {QStringLiteral("de"), QStringLiteral("de_DE")},
        {QStringLiteral("es"), QStringLiteral("es_ES")},
        {QStringLiteral("it"), QStringLiteral("it_IT")},
        {QStringLiteral("pt"), QStringLiteral("pt_BR")},
        {QStringLiteral("ja"), QStringLiteral("ja_JP")},
        {QStringLiteral("zh"), QStringLiteral("zh_CN")},
        {QStringLiteral("ko"), QStringLiteral("ko_KR")},
        {QStringLiteral("ru"), QStringLiteral("ru_RU")},
        {QStringLiteral("ar"), QStringLiteral("ar_SA")},
        {QStringLiteral("hi"), QStringLiteral("hi_IN")},
        {QStringLiteral("bn"), QStringLiteral("bn_BD")},
        {QStringLiteral("nl"), QStringLiteral("nl_NL")},
        {QStringLiteral("pl"), QStringLiteral("pl_PL")},
        {QStringLiteral("tr"), QStringLiteral("tr_TR")},
        {QStringLiteral("vi"), QStringLiteral("vi_VN")},
        {QStringLiteral("uk"), QStringLiteral("uk_UA")},
        {QStringLiteral("ro"), QStringLiteral("ro_RO")},
        {QStringLiteral("el"), QStringLiteral("el_GR")},
        {QStringLiteral("hu"), QStringLiteral("hu_HU")},
        {QStringLiteral("ms"), QStringLiteral("ms_MY")},
        {QStringLiteral("id"), QStringLiteral("id_ID")},
        {QStringLiteral("fa"), QStringLiteral("fa_IR")},
        {QStringLiteral("sw"), QStringLiteral("sw_KE")},
        {QStringLiteral("ta"), QStringLiteral("ta_IN")},
        {QStringLiteral("te"), QStringLiteral("te_IN")},
        {QStringLiteral("mr"), QStringLiteral("mr_IN")},
        {QStringLiteral("pa"), QStringLiteral("pa_IN")},
        {QStringLiteral("ur"), QStringLiteral("ur_PK")},
        {QStringLiteral("th"), QStringLiteral("th_TH")},
    };
    const auto it = map.constFind(lang);
    return it != map.cend() ? it.value() : lang + QLatin1Char('_') + lang.toUpper();
}

// Returns the "Updated" label for a given language code.
QString updatedLabel(const QString &lang)
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("en"), QStringLiteral("Updated")},
        {QStringLiteral("fr"), QStringLiteral("Mis à jour")},
        {QStringLiteral("de"), QStringLiteral("Aktualisiert")},
        {QStringLiteral("es"), QStringLiteral("Actualizado")},
        {QStringLiteral("it"), QStringLiteral("Aggiornato")},
        {QStringLiteral("pt"), QStringLiteral("Atualizado")},
        {QStringLiteral("nl"), QStringLiteral("Bijgewerkt")},
        {QStringLiteral("pl"), QStringLiteral("Zaktualizowano")},
        {QStringLiteral("ro"), QStringLiteral("Actualizat")},
        {QStringLiteral("tr"), QStringLiteral("Güncellendi")},
        {QStringLiteral("ru"), QStringLiteral("Обновлено")},
        {QStringLiteral("uk"), QStringLiteral("Оновлено")},
        {QStringLiteral("ar"), QStringLiteral("تم التحديث")},
        {QStringLiteral("ja"), QStringLiteral("更新日")},
        {QStringLiteral("zh"), QStringLiteral("更新于")},
        {QStringLiteral("ko"), QStringLiteral("업데이트")},
        {QStringLiteral("hi"), QStringLiteral("अपडेट किया गया")},
        {QStringLiteral("vi"), QStringLiteral("Cập nhật")},
        {QStringLiteral("id"), QStringLiteral("Diperbarui")},
        {QStringLiteral("ms"), QStringLiteral("Dikemas kini")},
        {QStringLiteral("th"), QStringLiteral("อัปเดต")},
    };
    const auto it = map.constFind(lang);
    return it != map.cend() ? it.value() : QStringLiteral("Updated");
}

// Resolves the date for langCode from the given per-language map, falling
// back to sourceLang when langCode is absent.
QString resolveDateForLang(const QHash<QString, QString> &byLang,
                           const QString                 &langCode,
                           const QString                 &sourceLang)
{
    const QString &direct = byLang.value(langCode);
    if (!direct.isEmpty()) {
        return direct;
    }
    return byLang.value(sourceLang);
}

} // namespace

QString PageTypeArticle::buildHeadMetaTags(const QString &baseUrl,
                                            const QString &langCode) const
{
    QString result;

    // ---- <title> + <meta name="description"> from PageBlocMeta ---------------
    const QString title = m_metaBloc.seoTitle(langCode);
    if (!title.isEmpty()) {
        result += QStringLiteral("<title>");
        result += title.toHtmlEscaped();
        result += QStringLiteral("</title>\n");
    }
    const QString desc = m_metaBloc.seoDescription(langCode);
    if (!desc.isEmpty()) {
        result += QStringLiteral("<meta name=\"description\" content=\"");
        result += desc.toHtmlEscaped();
        result += QStringLiteral("\"/>\n");
    }

    // ---- Canonical -----------------------------------------------------------
    if (!m_permalink.isEmpty()) {
        result += QStringLiteral("<link rel=\"canonical\" href=\"");
        result += baseUrl;
        result += m_permalink;
        result += QStringLiteral("\"/>\n");
    }

    // ---- Open Graph auto-derived tags ----------------------------------------
    if (!m_permalink.isEmpty()) {
        result += QStringLiteral("<meta property=\"og:url\" content=\"");
        result += baseUrl;
        result += m_permalink;
        result += QStringLiteral("\"/>\n");
    }
    result += QStringLiteral("<meta property=\"og:type\" content=\"article\"/>\n");
    if (!langCode.isEmpty()) {
        result += QStringLiteral("<meta property=\"og:locale\" content=\"");
        result += langToLocale(langCode);
        result += QStringLiteral("\"/>\n");
    }

    // ---- Per-platform social <meta> tags from PageBlocSocial -----------------
    // Only one Twitter card type per page: large-image when a landscape image
    // exists, summary otherwise. LinkedIn and Pinterest share og:title /
    // og:description with OpenGraph (they all read the same property), so only
    // OpenGraph emits those tags — LinkedIn and Pinterest contribute their
    // platform-specific image aspect ratios only.
    const bool hasLandscapeImage = !m_socialBloc.imgOg().isEmpty();
    for (const AbstractSocialMedia *platform : AbstractSocialMedia::all()) {
        const QString &id = platform->getId();

        if (id == QLatin1String("twitter") && !hasLandscapeImage) {
            continue;
        }
        if (id == QLatin1String("twitter_summary") && hasLandscapeImage) {
            continue;
        }

        QString platformTitle, platformDesc;
        if (id == QLatin1String("opengraph")) {
            platformTitle = m_socialTextBloc.facebookTitle();
            platformDesc  = m_socialTextBloc.facebookDesc();
        } else if (id == QLatin1String("twitter") || id == QLatin1String("twitter_summary")) {
            platformTitle = m_socialTextBloc.twitterTitle();
            platformDesc  = m_socialTextBloc.twitterDesc();
        }
        // linkedin / pinterest: image only — og:title/og:description are
        // already emitted by opengraph above.

        QString webpFilename;
        switch (platform->requiredImageSize()) {
        case AbstractSocialMedia::ImageSize::Landscape: webpFilename = m_socialBloc.imgOg();       break;
        case AbstractSocialMedia::ImageSize::Wide:      webpFilename = m_socialBloc.imgWide();     break;
        case AbstractSocialMedia::ImageSize::Square:    webpFilename = m_socialBloc.imgSquare();   break;
        case AbstractSocialMedia::ImageSize::Portrait:  webpFilename = m_socialBloc.imgPortrait(); break;
        }

        if (!webpFilename.isEmpty()) {
            result += platform->imageMetaTagHtml(baseUrl + QLatin1Char('/') + webpFilename);
        }
        if (!platformTitle.isEmpty()) {
            result += platform->titleMetaTagHtml(platformTitle);
        }
        if (!platformDesc.isEmpty()) {
            result += platform->descMetaTagHtml(platformDesc);
        }
    }

    // ---- Open Graph article dates -------------------------------------------
    const QString published = resolveDateForLang(m_publishedByLang, langCode, m_sourceLang);
    const QString updated   = resolveDateForLang(m_updatedByLang,   langCode, m_sourceLang);

    if (!published.isEmpty()) {
        result += QStringLiteral("<meta property=\"article:published_time\" content=\"");
        result += published;
        result += QStringLiteral("\"/>\n");
    }
    if (!updated.isEmpty()) {
        result += QStringLiteral("<meta property=\"article:modified_time\" content=\"");
        result += updated;
        result += QStringLiteral("\"/>\n");
    }

    // ---- JSON-LD Article (highest-value signal for Google) ------------------
    if (!published.isEmpty() || !updated.isEmpty()) {
        result += QStringLiteral("<script type=\"application/ld+json\">"
                                  "{\"@context\":\"https://schema.org\","
                                  "\"@type\":\"Article\"");

        // headline — the SEO title for this language
        const QString headline = m_metaBloc.seoTitle(langCode);
        if (!headline.isEmpty()) {
            result += QStringLiteral(",\"headline\":\"");
            result += headline.toHtmlEscaped();
            result += QLatin1Char('"');
        }

        if (!published.isEmpty()) {
            result += QStringLiteral(",\"datePublished\":\"");
            result += published;
            result += QLatin1Char('"');
        }
        if (!updated.isEmpty()) {
            result += QStringLiteral(",\"dateModified\":\"");
            result += updated;
            result += QLatin1Char('"');
        }
        if (!m_permalink.isEmpty() && !baseUrl.isEmpty()) {
            result += QStringLiteral(",\"url\":\"");
            result += baseUrl;
            result += m_permalink;
            result += QLatin1Char('"');
        }

        // image — cascade: OG WebP → wide → square → portrait → auto-rasterized fallback
        const QString &jsonLdImage = !m_socialBloc.imgOg().isEmpty()       ? m_socialBloc.imgOg()
                                   : !m_socialBloc.imgWide().isEmpty()     ? m_socialBloc.imgWide()
                                   : !m_socialBloc.imgSquare().isEmpty()   ? m_socialBloc.imgSquare()
                                   : !m_socialBloc.imgPortrait().isEmpty() ? m_socialBloc.imgPortrait()
                                   :                                         m_jsonLdFallbackImage;
        if (!jsonLdImage.isEmpty() && !baseUrl.isEmpty()) {
            result += QStringLiteral(",\"image\":\"");
            result += baseUrl;
            result += QLatin1Char('/');
            result += jsonLdImage;
            result += QLatin1Char('"');
        }

        // author — Person node
        if (!m_websiteAuthor.isEmpty()) {
            result += QStringLiteral(",\"author\":{\"@type\":\"Person\",\"name\":\"");
            result += m_websiteAuthor.toHtmlEscaped();
            result += QStringLiteral("\"}");
        }

        // publisher — Organization node with optional logo
        if (!m_websiteName.isEmpty()) {
            result += QStringLiteral(",\"publisher\":{\"@type\":\"Organization\",\"name\":\"");
            result += m_websiteName.toHtmlEscaped();
            result += QLatin1Char('"');
            // logo — SVG favicon URL when available
            if (!baseUrl.isEmpty()) {
                // Look up the favicon through the theme via the engine — not available here,
                // so emit a conventional /favicon.svg URL; it is served when the favicon blob
                // was uploaded (domain="" fallback in ImageRepositorySQLite).
                result += QStringLiteral(",\"logo\":{\"@type\":\"ImageObject\",\"url\":\"");
                result += baseUrl;
                result += QStringLiteral("/favicon.svg\"}");
            }
            result += QLatin1Char('}');
        }

        result += QStringLiteral("}</script>\n");
    }

    // ---- JSON-LD BreadcrumbList ---------------------------------------------
    // Home > Category > Article — helps Google show rich breadcrumbs in SERPs.
    if (!baseUrl.isEmpty() && !m_permalink.isEmpty()) {
        const QList<PageBlocCategory::BreadcrumbItem> chain =
            m_categoryBloc->primaryChain(langCode);

        // Item count: 1 (Home) + category chain + 1 (article)
        const int total = 1 + chain.size() + 1;
        result += QStringLiteral("<script type=\"application/ld+json\">"
                                  "{\"@context\":\"https://schema.org\","
                                  "\"@type\":\"BreadcrumbList\","
                                  "\"itemListElement\":[");

        // Position 1 — Home
        result += QStringLiteral("{\"@type\":\"ListItem\",\"position\":1,"
                                  "\"name\":\"Home\",\"item\":\"");
        result += baseUrl;
        result += QStringLiteral("\"}");

        // Positions 2…N — category chain
        int pos = 2;
        for (const auto &item : std::as_const(chain)) {
            result += QStringLiteral(",{\"@type\":\"ListItem\",\"position\":");
            result += QString::number(pos++);
            result += QStringLiteral(",\"name\":\"");
            result += item.name.toHtmlEscaped();
            result += QStringLiteral("\",\"item\":\"");
            result += baseUrl;
            result += item.permalink;
            result += QStringLiteral("\"}");
        }

        // Last position — article
        const QString headline = m_metaBloc.seoTitle(langCode);
        result += QStringLiteral(",{\"@type\":\"ListItem\",\"position\":");
        result += QString::number(total);
        result += QStringLiteral(",\"name\":\"");
        result += (headline.isEmpty() ? m_permalink : headline).toHtmlEscaped();
        result += QStringLiteral("\",\"item\":\"");
        result += baseUrl;
        result += m_permalink;
        result += QStringLiteral("\"}");

        result += QStringLiteral("]}</script>\n");
    }

    return result;
}

bool PageTypeArticle::hasSvg() const { return true; }

void PageTypeArticle::addInnerTopCode(AbstractEngine &engine,
                                          int             websiteIndex,
                                          QString        &html,
                                          QString        &css,
                                          QString        &js,
                                          QSet<QString>  &cssDoneIds,
                                          QSet<QString>  &jsDoneIds) const
{
    AbstractTheme *theme = engine.getActiveTheme();
    if (theme) {
        theme->addCodeArticle(engine, websiteIndex, html, css, js, cssDoneIds, jsDoneIds);
    }

    // Visible date line — injected once per page at the article level.
    static const QString cssId = QStringLiteral("article-date-css");
    if (!cssDoneIds.contains(cssId)) {
        cssDoneIds.insert(cssId);
        css += QStringLiteral(".article-date{"
                               "font-size:.85em;color:#6b7280;"
                               "margin-bottom:1rem;display:block}"
                               ".article-date time{font-style:normal}");
    }
    Q_UNUSED(js)
    Q_UNUSED(jsDoneIds)

    const QString langCode  = engine.getLangCode(websiteIndex);
    const QString published = resolveDateForLang(m_publishedByLang, langCode, m_sourceLang);
    const QString updated   = resolveDateForLang(m_updatedByLang,   langCode, m_sourceLang);

    if (published.isEmpty()) {
        return;
    }

    const QDateTime publishedDt = QDateTime::fromString(published, Qt::ISODate);
    if (!publishedDt.isValid()) {
        return;
    }

    const QLocale locale(langCode);
    const QString publishedFormatted = locale.toString(publishedDt.date(), QLocale::LongFormat);

    html += QStringLiteral("<span class=\"article-date\">"
                            "<time datetime=\"");
    html += published;
    html += QStringLiteral("\">");
    html += publishedFormatted.toHtmlEscaped();
    html += QStringLiteral("</time>");

    // Show "Updated" only when last modification is more than 30 days after
    // creation — minor edits are not worth surfacing to readers.
    if (!updated.isEmpty() && updated != published) {
        const QDateTime updatedDt = QDateTime::fromString(updated, Qt::ISODate);
        if (updatedDt.isValid() && publishedDt.daysTo(updatedDt) > 30) {
            const QString updatedFormatted = locale.toString(updatedDt.date(), QLocale::LongFormat);
            html += QStringLiteral(" · ");
            html += updatedLabel(langCode);
            html += QStringLiteral(" <time datetime=\"");
            html += updated;
            html += QStringLiteral("\">");
            html += updatedFormatted.toHtmlEscaped();
            html += QStringLiteral("</time>");
        }
    }

    html += QStringLiteral("</span>");
}

DECLARE_PAGE_TYPE(PageTypeArticle)
