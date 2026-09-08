#include "PageTypeArticleFashion.h"

PageTypeArticleFashion::PageTypeArticleFashion(CategoryTable &categoryTable)
    : PageTypeArticleBase(categoryTable)
{
    m_blocs.append(&m_fashionTaxonomyLinksBloc); // 7 — Color/Season/Occasion/Material/Style tag links
}

PageTypeArticleFashion::~PageTypeArticleFashion() = default;

QString PageTypeArticleFashion::getTypeId()      const { return QLatin1String(TYPE_ID); }
QString PageTypeArticleFashion::getDisplayName() const { return QLatin1String(DISPLAY_NAME); }

void PageTypeArticleFashion::bindGenerationContext(IPageRepository & /*repo*/, const QDir &workingDir)
{
    bindWorkingDir(workingDir);
}

void PageTypeArticleFashion::bindWorkingDir(const QDir &workingDir)
{
    m_fashionTaxonomyLinksBloc.setWorkingDir(workingDir);
}

DECLARE_PAGE_TYPE(PageTypeArticleFashion)
