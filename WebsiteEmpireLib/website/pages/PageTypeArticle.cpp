#include "PageTypeArticle.h"

#include "website/pages/blocs/PageBlocCategory.h"
#include "website/social/AbstractSocialMedia.h"

#include <QHash>

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

    // ---- hreflang links ------------------------------------------------------
    // When this is a target-language page its permalink starts with /{lang}/.
    // Strip that prefix to recover the source permalink, then reconstruct all
    // language variants.  This covers the common /{lang}/slug convention; pages
    // with custom translated slugs will have slightly imprecise hreflang links.
    if (!m_permalink.isEmpty() && !m_sourceLang.isEmpty()) {
        QString sourcePermalink = m_permalink;
        if (!langCode.isEmpty() && langCode != m_sourceLang) {
            const QString prefix = QLatin1Char('/') + langCode + QLatin1Char('/');
            if (sourcePermalink.startsWith(prefix)) {
                sourcePermalink = sourcePermalink.mid(langCode.size() + 1); // keep leading /
            }
        }

        result += QStringLiteral("<link rel=\"alternate\" hreflang=\"x-default\" href=\"");
        result += baseUrl;
        result += sourcePermalink;
        result += QStringLiteral("\"/>\n");

        result += QStringLiteral("<link rel=\"alternate\" hreflang=\"");
        result += m_sourceLang;
        result += QStringLiteral("\" href=\"");
        result += baseUrl;
        result += sourcePermalink;
        result += QStringLiteral("\"/>\n");

        for (const QString &tl : std::as_const(m_targetLangs)) {
            result += QStringLiteral("<link rel=\"alternate\" hreflang=\"");
            result += tl;
            result += QStringLiteral("\" href=\"");
            result += baseUrl;
            result += QLatin1Char('/');
            result += tl;
            result += sourcePermalink;
            result += QStringLiteral("\"/>\n");
        }
    }

    // ---- Per-platform social <meta> tags from PageBlocSocial -----------------
    for (const AbstractSocialMedia *platform : AbstractSocialMedia::all()) {
        const QString &id = platform->getId();

        QString platformTitle, platformDesc;
        if (id == QLatin1String("opengraph")) {
            platformTitle = m_socialTextBloc.facebookTitle();
            platformDesc  = m_socialTextBloc.facebookDesc();
        } else if (id == QLatin1String("twitter") || id == QLatin1String("twitter_summary")) {
            platformTitle = m_socialTextBloc.twitterTitle();
            platformDesc  = m_socialTextBloc.twitterDesc();
        } else if (id == QLatin1String("pinterest")) {
            platformTitle = m_socialTextBloc.pinterestTitle();
            platformDesc  = m_socialTextBloc.pinterestDesc();
        } else if (id == QLatin1String("linkedin")) {
            platformTitle = m_socialTextBloc.linkedinTitle();
            platformDesc  = m_socialTextBloc.linkedinDesc();
        }

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

    return result;
}

bool PageTypeArticle::hasSvg() const { return true; }

DECLARE_PAGE_TYPE(PageTypeArticle)
