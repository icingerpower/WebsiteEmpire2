#include "EngineLanguages.h"

#include "CountryLangManager.h"
#include "website/pages/PageTypeArticle.h"
#include "website/pages/attributes/CategoryTable.h"

#include <QLocale>

DECLARE_ENGINE(EngineLanguages)

EngineLanguages::EngineLanguages(QObject *parent)
    : AbstractEngine(parent)
{
}

EngineLanguages::~EngineLanguages() = default;

QString EngineLanguages::getId() const
{
    return QStringLiteral("EngineLanguages");
}

QString EngineLanguages::getGeneratorId() const
{
    return QStringLiteral("language-learning");
}

QString EngineLanguages::getName() const
{
    return tr("Languages");
}

AbstractEngine *EngineLanguages::create(QObject *parent) const
{
    return new EngineLanguages(parent);
}

QList<AbstractEngine::Variation> EngineLanguages::getVariations() const
{
    const QStringList codes = CountryLangManager::instance()->defaultLangCodes();
    QList<Variation> vars;
    vars.reserve(codes.size());
    for (const QString &code : codes) {
        const QLocale locale(code);
        vars.append({ code, QLocale::languageToString(locale.language()) });
    }
    return vars;
}

const QList<const AbstractPageType *> &EngineLanguages::getPageTypes() const
{
    return m_pageTypes;
}

CategoryTable &EngineLanguages::categoryTable() const
{
    Q_ASSERT(m_categoryTable);
    return *m_categoryTable;
}

void EngineLanguages::_onInit(const QDir &workingDir)
{
    m_articleType.reset();   // release CategoryTable & before destroying the table
    m_categoryTable.reset(new CategoryTable(workingDir));
    m_articleType.reset(new PageTypeArticle(*m_categoryTable));
    m_pageTypes.clear();
    m_pageTypes.append(m_articleType.data());
}
