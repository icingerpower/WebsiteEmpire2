#include "PageTypeArticle.h"

PageTypeArticle::PageTypeArticle()
{
    m_blocs.append(&m_textBloc);
}

const QList<const AbstractPageBloc *> &PageTypeArticle::getPageBlocs() const
{
    return m_blocs;
}
