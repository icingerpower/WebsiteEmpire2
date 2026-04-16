#ifndef ENGINELANGUAGES_H
#define ENGINELANGUAGES_H

#include "AbstractEngine.h"

#include <QList>
#include <QScopedPointer>

class CategoryTable;
class PageTypeArticle;

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
    ~EngineLanguages() override;

    QString          getId()          const override;
    QString          getName()        const override;
    QList<Variation> getVariations()  const override;
    AbstractEngine  *create(QObject *parent = nullptr) const override;
    QString          getGeneratorId() const override;

    const QList<const AbstractPageType *> &getPageTypes() const override;

    // Returns the category vocabulary for this engine's page types.
    // Valid only after init() has been called.
    CategoryTable &categoryTable() const;

protected:
    void _onInit(const QDir &workingDir) override;

private:
    QScopedPointer<CategoryTable>          m_categoryTable;
    QScopedPointer<PageTypeArticle>        m_articleType;
    QList<const AbstractPageType *>        m_pageTypes;
};

#endif // ENGINELANGUAGES_H
