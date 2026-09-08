#include "EngineArticlesFashion.h"

#include "website/pages/PageTypeArticleFashion.h"
#include "website/pages/attributes/CategoryTable.h"

DECLARE_ENGINE(EngineArticlesFashion)

EngineArticlesFashion::EngineArticlesFashion(QObject *parent)
    : AbstractEngine(parent)
{
}

EngineArticlesFashion::~EngineArticlesFashion() = default;

QString EngineArticlesFashion::getId() const
{
    return QStringLiteral("EngineArticlesFashion");
}

QString EngineArticlesFashion::getName() const
{
    return tr("Articles (Fashion)");
}

QString EngineArticlesFashion::getGeneratorId() const
{
    return QStringLiteral("fashion_taxonomy"); // GeneratorFashionTaxonomy::getId()
}

AbstractEngine *EngineArticlesFashion::create(QObject *parent) const
{
    return new EngineArticlesFashion(parent);
}

QList<AbstractEngine::Variation> EngineArticlesFashion::getVariations() const
{
    return {{ QStringLiteral("default"), tr("Default") }};
}

const QList<const AbstractPageType *> &EngineArticlesFashion::getPageTypes() const
{
    return m_pageTypes;
}

CategoryTable &EngineArticlesFashion::categoryTable() const
{
    Q_ASSERT(m_categoryTable);
    return *m_categoryTable;
}

void EngineArticlesFashion::_onInit(const QDir &workingDir)
{
    // Release the page type before destroying the category table it references.
    m_articleType.reset();
    m_categoryTable.reset(new CategoryTable(workingDir));
    m_articleType.reset(new PageTypeArticleFashion(*m_categoryTable));
    m_pageTypes.clear();
    m_pageTypes.append(m_articleType.data());
}
