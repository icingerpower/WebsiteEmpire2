#include "PageTypeHome.h"

#include "website/pages/blocs/PageBlocCategory.h"

PageTypeHome::PageTypeHome(CategoryTable &categoryTable)
    : m_categoryBloc(new PageBlocCategory(categoryTable))
{
    m_blocs.append(&m_textBlocTop);
    m_blocs.append(&m_imageLinksBloc);
    m_blocs.append(m_categoryBloc.data());
    m_blocs.append(&m_textBlocBottom);
}

PageTypeHome::~PageTypeHome() = default;

QString PageTypeHome::getTypeId()      const { return QLatin1String(TYPE_ID); }
QString PageTypeHome::getDisplayName() const { return QLatin1String(DISPLAY_NAME); }

const QList<const AbstractPageBloc *> &PageTypeHome::getPageBlocs() const
{
    return m_blocs;
}

DECLARE_PAGE_TYPE(PageTypeHome)
