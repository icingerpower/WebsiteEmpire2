#include "EngineArticles.h"

#include "website/pages/PageTypeArticle.h"
#include "website/pages/attributes/CategoryTable.h"

DECLARE_ENGINE(EngineArticles)

EngineArticles::EngineArticles(QObject *parent)
    : AbstractEngine(parent)
{
}

EngineArticles::~EngineArticles() = default;

QString EngineArticles::getId() const
{
    return QStringLiteral("EngineArticles");
}

QString EngineArticles::getName() const
{
    return tr("Articles");
}

AbstractEngine *EngineArticles::create(QObject *parent) const
{
    return new EngineArticles(parent);
}

QList<AbstractEngine::Variation> EngineArticles::getVariations() const
{
    return {{ QStringLiteral("default"), tr("Default") }};
}

const QList<const AbstractPageType *> &EngineArticles::getPageTypes() const
{
    return m_pageTypes;
}

CategoryTable &EngineArticles::categoryTable() const
{
    Q_ASSERT(m_categoryTable);
    return *m_categoryTable;
}

void EngineArticles::_onInit(const QDir &workingDir)
{
    m_articleType.reset();   // release CategoryTable & before destroying the table
    m_categoryTable.reset(new CategoryTable(workingDir));
    m_articleType.reset(new PageTypeArticle(*m_categoryTable));
    m_pageTypes.clear();
    m_pageTypes.append(m_articleType.data());
}
