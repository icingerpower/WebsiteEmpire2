#include "EngineLanguages.h"
#include "CountryLangManager.h"

#include <QLocale>

DECLARE_ENGINE(EngineLanguages)

EngineLanguages::EngineLanguages(QObject *parent)
    : AbstractEngine(parent)
{
}

QString EngineLanguages::getId() const
{
    return QStringLiteral("EngineLanguages");
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
