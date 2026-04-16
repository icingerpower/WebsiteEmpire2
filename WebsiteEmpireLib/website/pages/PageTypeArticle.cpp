#include "PageTypeArticle.h"

#include "website/pages/blocs/PageBlocCategory.h"

PageTypeArticle::PageTypeArticle(CategoryTable &categoryTable)
    : m_categoryBloc(new PageBlocCategory(categoryTable))
{
    m_blocs.append(m_categoryBloc.data());
    m_blocs.append(&m_textBloc);
    m_blocs.append(&m_socialBloc);
    m_blocs.append(&m_autoLinkBloc);
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

DECLARE_PAGE_TYPE(PageTypeArticle)
