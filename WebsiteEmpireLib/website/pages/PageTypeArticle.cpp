#include "PageTypeArticle.h"

#include "website/pages/blocs/PageBlocCategory.h"
#include "website/social/AbstractSocialMedia.h"

PageTypeArticle::PageTypeArticle(CategoryTable &categoryTable)
    : m_categoryBloc(new PageBlocCategory(categoryTable))
    , m_categoryLinksBloc(categoryTable)
{
    m_blocs.append(m_categoryBloc.data()); // 0
    m_blocs.append(&m_textBloc);           // 1
    m_blocs.append(&m_socialTextBloc);     // 2 — text metadata, first pass
    m_blocs.append(&m_autoLinkBloc);       // 3
    m_blocs.append(&m_categoryLinksBloc);  // 4
    m_blocs.append(&m_socialBloc);         // 5 — image variants, second pass
}

PageTypeArticle::~PageTypeArticle() = default;

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

QString PageTypeArticle::buildHeadMetaTags(const QString &baseUrl) const
{
    QString result;

    for (const AbstractSocialMedia *platform : AbstractSocialMedia::all()) {
        const QString &id = platform->getId();

        // Select per-platform title and description.
        QString title, desc;
        if (id == QLatin1String("opengraph")) {
            title = m_socialTextBloc.facebookTitle();
            desc  = m_socialTextBloc.facebookDesc();
        } else if (id == QLatin1String("twitter") || id == QLatin1String("twitter_summary")) {
            title = m_socialTextBloc.twitterTitle();
            desc  = m_socialTextBloc.twitterDesc();
        } else if (id == QLatin1String("pinterest")) {
            title = m_socialTextBloc.pinterestTitle();
            desc  = m_socialTextBloc.pinterestDesc();
        } else if (id == QLatin1String("linkedin")) {
            title = m_socialTextBloc.linkedinTitle();
            desc  = m_socialTextBloc.linkedinDesc();
        }

        // Image URL: derive from the WebP filename stored after second-pass generation.
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

bool PageTypeArticle::hasSvg() const { return true; }

DECLARE_PAGE_TYPE(PageTypeArticle)
