#include "PageTypeCategory.h"

#include "website/pages/IPageRepository.h"
#include "website/social/AbstractSocialMedia.h"

#include <QDir>

PageTypeCategory::PageTypeCategory(CategoryTable &categoryTable)
    : m_hubGridBloc(categoryTable)
{
    m_blocs.append(&m_hubGridBloc);
    m_blocs.append(&m_socialBloc);
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

QString PageTypeCategory::buildHeadMetaTags(const QString &baseUrl) const
{
    QString result;

    for (const AbstractSocialMedia *platform : AbstractSocialMedia::all()) {
        const QString &id = platform->getId();

        QString title, desc;
        if (id == QLatin1String("opengraph")) {
            title = m_socialBloc.facebookTitle();
            desc  = m_socialBloc.facebookDesc();
        } else if (id == QLatin1String("twitter") || id == QLatin1String("twitter_summary")) {
            title = m_socialBloc.twitterTitle();
            desc  = m_socialBloc.twitterDesc();
        } else if (id == QLatin1String("pinterest")) {
            title = m_socialBloc.pinterestTitle();
            desc  = m_socialBloc.pinterestDesc();
        } else if (id == QLatin1String("linkedin")) {
            title = m_socialBloc.linkedinTitle();
            desc  = m_socialBloc.linkedinDesc();
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

    return result;
}

DECLARE_PAGE_TYPE(PageTypeCategory)
