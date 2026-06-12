#include "PageTypeCategory.h"

#include "website/pages/IPageRepository.h"
#include "website/social/AbstractSocialMedia.h"

#include <QDir>

PageTypeCategory::PageTypeCategory(CategoryTable &categoryTable)
    : m_hubGridBloc(categoryTable)
{
    m_blocs.append(&m_hubGridBloc);    // 0
    m_blocs.append(&m_socialTextBloc); // 1 — text metadata, first pass
    m_blocs.append(&m_socialBloc);     // 2 — image variants, second pass
}

QString PageTypeCategory::getTypeId()      const { return QLatin1String(TYPE_ID); }
QString PageTypeCategory::getDisplayName() const { return QLatin1String(DISPLAY_NAME); }

const QList<const AbstractPageBloc *> &PageTypeCategory::getPageBlocs() const
{
    return m_blocs;
}

void PageTypeCategory::bindGenerationContext(IPageRepository &repo, const QDir &workingDir)
{
    m_hubGridBloc.bindContext(repo, workingDir);
}

QString PageTypeCategory::buildHeadMetaTags(const QString &baseUrl, const QString &langCode) const
{
    QString result;

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

DECLARE_PAGE_TYPE(PageTypeCategory)
