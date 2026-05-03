#include "PageTypeCategory.h"

#include "website/pages/IPageRepository.h"

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

DECLARE_PAGE_TYPE(PageTypeCategory)
