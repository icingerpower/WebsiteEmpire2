#ifndef ENGINELANGUAGES_H
#define ENGINELANGUAGES_H

#include "AbstractEngine.h"

// Engine for language-learning websites.
// getVariations() returns every lang code from CountryLangManager as a source
// language.  Combined with the target-language rows this produces an
// N × N matrix: one row per (target-language, source-language) pair, e.g.
// (fr, en) = "Learn French from English".
class EngineLanguages : public AbstractEngine
{
    Q_OBJECT
public:
    explicit EngineLanguages(QObject *parent = nullptr);

    QString          getId()         const override;
    QString          getName()       const override;
    QList<Variation> getVariations() const override;
    AbstractEngine  *create(QObject *parent = nullptr) const override;
};

#endif // ENGINELANGUAGES_H
