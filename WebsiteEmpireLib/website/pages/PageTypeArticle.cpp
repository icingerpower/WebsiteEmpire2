#include "PageTypeArticle.h"

#include "website/pages/blocs/PageBlocCategory.h"

PageTypeArticle::PageTypeArticle(CategoryTable &categoryTable)
    : m_categoryBloc(new PageBlocCategory(categoryTable))
{
    m_blocs.append(m_categoryBloc.data());
    m_blocs.append(&m_textBloc);
}

PageTypeArticle::~PageTypeArticle() = default;

const QList<const AbstractPageBloc *> &PageTypeArticle::getPageBlocs() const
{
    return m_blocs;
}
