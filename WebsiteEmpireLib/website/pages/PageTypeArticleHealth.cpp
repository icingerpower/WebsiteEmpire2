#include "PageTypeArticleHealth.h"

// =============================================================================
// Constructor / Destructor
// =============================================================================

PageTypeArticleHealth::PageTypeArticleHealth(CategoryTable &categoryTable)
    : PageTypeArticleBase(categoryTable)
{
    m_blocs.append(&m_symptomLinksBloc);   // 7 — editor-selected symptom pill links
}

PageTypeArticleHealth::~PageTypeArticleHealth() = default;

// =============================================================================
// bindGenerationContext
// =============================================================================

void PageTypeArticleHealth::bindGenerationContext(IPageRepository & /*repo*/, const QDir &workingDir)
{
    bindWorkingDir(workingDir);
}

void PageTypeArticleHealth::bindWorkingDir(const QDir &workingDir)
{
    m_symptomLinksBloc.setWorkingDir(workingDir);
}

// =============================================================================
// Accessors
// =============================================================================

QString PageTypeArticleHealth::getTypeId()      const { return QLatin1String(TYPE_ID); }
QString PageTypeArticleHealth::getDisplayName() const { return QLatin1String(DISPLAY_NAME); }

QList<const AbstractPageBloc *> PageTypeArticleHealth::getRenderBlocs() const
{
    // Storage order: 0=category 1=text 2=social 3=autolink 4=categorylinks
    //                5=socialmedia 6=meta 7=symptomlinks
    // Render order: symptomlinks inserted at position 1 (after category, before text)
    // so the pill links appear above the article title without changing data keys.
    QList<const AbstractPageBloc *> order;
    order.reserve(m_blocs.size());
    order.append(m_blocs.at(0));               // category
    order.append(m_blocs.at(7));               // symptomlinks — before text
    for (int i = 1; i <= 6; ++i) {
        order.append(m_blocs.at(i));
    }
    return order;
}

DECLARE_PAGE_TYPE(PageTypeArticleHealth)
