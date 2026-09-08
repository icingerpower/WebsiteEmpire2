#include "PageTypeFashionTagHub.h"

#include "website/pages/IPageRepository.h"
#include "website/social/AbstractSocialMedia.h"

#include <QMap>

PageTypeFashionTagHub::PageTypeFashionTagHub(CategoryTable & /*categoryTable*/)
{
    m_blocs.append(&m_hubGridBloc);    // 0
    m_blocs.append(&m_socialTextBloc); // 1 — text metadata, first pass
    m_blocs.append(&m_socialBloc);     // 2 — image variants, second pass
}

QString PageTypeFashionTagHub::getTypeId()      const { return QLatin1String(TYPE_ID); }
QString PageTypeFashionTagHub::getDisplayName() const { return QLatin1String(DISPLAY_NAME); }

const QList<const AbstractPageBloc *> &PageTypeFashionTagHub::getPageBlocs() const
{
    return m_blocs;
}

void PageTypeFashionTagHub::bindGenerationContext(IPageRepository &repo, const QDir &workingDir)
{
    AbstractPageType::bindGenerationContext(repo, workingDir);
    m_hubGridBloc.bindContext(repo, workingDir);
}

// =============================================================================
// seoTemplateStrings / autoSeoTitle / autoSeoDescription
// =============================================================================

QMap<QString, QString> PageTypeFashionTagHub::seoTemplateStrings() const
{
    return {
        { QStringLiteral("title"),         QStringLiteral("%1 %2 Outfit Ideas: %3 To Try") },
        { QStringLiteral("title_nocount"), QStringLiteral("%1 %2 Outfit Ideas") },
        { QStringLiteral("desc"),          QStringLiteral("Browse %1 outfit ideas featuring %2 %3 — %4 looks to inspire your next fit.") },
    };
}

QString PageTypeFashionTagHub::autoSeoTitle(const QString &langCode) const
{
    const QString name = m_hubGridBloc.translatedTagName(langCode);
    if (name.isEmpty()) {
        return {};
    }
    const int n = m_hubGridBloc.lastRenderedCount();
    const QString tmpl = seoTemplate(
        n <= 0 ? QStringLiteral("title_nocount") : QStringLiteral("title"), langCode);
    if (tmpl.isEmpty()) {
        return {};
    }
    if (n <= 0) {
        return tmpl.arg(name, m_hubGridBloc.dimension());
    }
    return tmpl.arg(name, m_hubGridBloc.dimension(), QString::number(n));
}

QString PageTypeFashionTagHub::autoSeoDescription(const QString &langCode) const
{
    const QString name = m_hubGridBloc.translatedTagName(langCode);
    if (name.isEmpty()) {
        return {};
    }
    const int n = m_hubGridBloc.lastRenderedCount();
    if (n <= 0) {
        return {};
    }
    const QString tmpl = seoTemplate(QStringLiteral("desc"), langCode);
    if (tmpl.isEmpty()) {
        return {};
    }
    return tmpl.arg(QString::number(n), name.toLower(),
                    m_hubGridBloc.dimension().toLower(), QString::number(n));
}

// =============================================================================
// buildHeadMetaTags
// =============================================================================

QString PageTypeFashionTagHub::buildHeadMetaTags(const QString &baseUrl, const QString &langCode, const QString &canonicalPath) const
{
    // Base emits: <title>, <meta name="description">, canonical, og:url.
    QString result = AbstractPageType::buildHeadMetaTags(baseUrl, langCode, canonicalPath);

    result += QStringLiteral("<meta property=\"og:type\" content=\"website\">");

    const bool hasLandscapeImage = !m_socialBloc.imgOg().isEmpty();
    for (const AbstractSocialMedia *platform : AbstractSocialMedia::all()) {
        const QString &id = platform->getId();

        if (id == QLatin1String("twitter") && !hasLandscapeImage) {
            continue;
        }
        if (id == QLatin1String("twitter_summary") && hasLandscapeImage) {
            continue;
        }

        QString title, desc;
        if (id == QLatin1String("opengraph")) {
            title = m_socialTextBloc.facebookTitle();
            desc  = m_socialTextBloc.facebookDesc();
        } else if (id == QLatin1String("twitter") || id == QLatin1String("twitter_summary")) {
            title = m_socialTextBloc.twitterTitle();
            desc  = m_socialTextBloc.twitterDesc();
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
        if (!title.isEmpty()) {
            result += platform->titleMetaTagHtml(title);
        }
        if (!desc.isEmpty()) {
            result += platform->descMetaTagHtml(desc);
        }
    }

    // ---- JSON-LD WebPage dateModified (hub pages evolve; no published date) -
    const QString &updated = m_updatedByLang.contains(langCode)
                             ? m_updatedByLang.value(langCode)
                             : m_updatedByLang.value(m_sourceLang);
    if (!updated.isEmpty()) {
        result += QStringLiteral("<script type=\"application/ld+json\">"
                                  "{\"@context\":\"https://schema.org\","
                                  "\"@type\":\"WebPage\","
                                  "\"dateModified\":\"");
        result += updated;
        result += QLatin1Char('"');
        if (!m_permalink.isEmpty() && !baseUrl.isEmpty()) {
            result += QStringLiteral(",\"url\":\"");
            result += baseUrl;
            result += m_permalink;
            result += QLatin1Char('"');
        }
        result += QStringLiteral("}</script>\n");
    }

    return result;
}

DECLARE_PAGE_TYPE_AUTO_MANAGED(PageTypeFashionTagHub)
