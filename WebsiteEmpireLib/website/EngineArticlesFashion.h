#ifndef ENGINEARTICLESFASHION_H
#define ENGINEARTICLESFASHION_H

#include "AbstractEngine.h"

#include <QList>
#include <QScopedPointer>

class CategoryTable;
class PageTypeArticleFashion;

// Fashion-vertical articles engine.
//
// A single "default" variation produces one domain row per target language.
// Page types: PageTypeArticleFashion only (the seven generic article blocs,
// no health-specific symptom-links bloc) — see EngineArticles for the
// equivalent Health-vertical engine this mirrors.
//
// After init(), categoryTable() gives access to the shared category vocabulary
// that backs the page type's category bloc.
class EngineArticlesFashion : public AbstractEngine
{
    Q_OBJECT
public:
    explicit EngineArticlesFashion(QObject *parent = nullptr);
    ~EngineArticlesFashion() override;

    QString          getId()         const override;
    QString          getName()       const override;
    QString          getGeneratorId() const override;
    QList<Variation> getVariations() const override;
    AbstractEngine  *create(QObject *parent = nullptr) const override;

    const QList<const AbstractPageType *> &getPageTypes() const override;

    // Returns the category vocabulary for this engine's page type.
    // Valid only after init() has been called.
    CategoryTable &categoryTable() const;

protected:
    void _onInit(const QDir &workingDir) override;

private:
    QScopedPointer<CategoryTable>           m_categoryTable;
    QScopedPointer<PageTypeArticleFashion>  m_articleType;
    QList<const AbstractPageType *>         m_pageTypes;
};

#endif // ENGINEARTICLESFASHION_H
