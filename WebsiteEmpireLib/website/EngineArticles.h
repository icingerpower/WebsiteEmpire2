#ifndef ENGINEARTICLES_H
#define ENGINEARTICLES_H

#include "AbstractEngine.h"

#include <QList>
#include <QScopedPointer>

class CategoryTable;
class PageTypeArticle;

// General-purpose articles engine.
//
// A single "default" variation produces one domain row per target language.
// Page types: PageTypeArticle (category bloc + text bloc).
//
// After init(), categoryTable() gives access to the shared category vocabulary
// that backs the article page type's category bloc.
class EngineArticles : public AbstractEngine
{
    Q_OBJECT
public:
    explicit EngineArticles(QObject *parent = nullptr);
    ~EngineArticles() override;

    QString          getId()         const override;
    QString          getName()       const override;
    QList<Variation> getVariations() const override;
    AbstractEngine  *create(QObject *parent = nullptr) const override;

    const QList<const AbstractPageType *> &getPageTypes() const override;

    // Returns the category vocabulary for this engine's page types.
    // Valid only after init() has been called.
    CategoryTable &categoryTable() const;

protected:
    void _onInit(const QDir &workingDir) override;

private:
    QScopedPointer<CategoryTable>   m_categoryTable;
    QScopedPointer<PageTypeArticle> m_articleType;
    QList<const AbstractPageType *> m_pageTypes;
};

#endif // ENGINEARTICLES_H
