#ifndef PAGETYPEARTICLEHEALTH_H
#define PAGETYPEARTICLEHEALTH_H

#include "website/pages/PageTypeArticleBase.h"
#include "website/pages/blocs/PageBlocSymptomLinks.h"

class CategoryTable;

/**
 * Health-vertical article page type.
 *
 * Adds one bloc to PageTypeArticleBase's seven generic ones:
 *   7 — PageBlocSymptomLinks : editor-selected symptom pill links
 *
 * Registered in the AbstractPageType registry under TYPE_ID = "article" —
 * this string is persisted in pages.typeId, strategies.json's pageTypeId,
 * and every createForTypeId("article", ...) call site across the app. It
 * must never change, even though this C++ class was renamed from the
 * original (pre-vertical-split) PageTypeArticle — changing TYPE_ID would
 * silently orphan every already-generated page.
 *
 * bindGenerationContext() stores the working directory so that addCode() can
 * supply it to PageBlocSymptomLinks for aspire DB queries.
 */
class PageTypeArticleHealth : public PageTypeArticleBase
{
public:
    static constexpr const char *TYPE_ID      = "article";
    static constexpr const char *DISPLAY_NAME = "Article";

    explicit PageTypeArticleHealth(CategoryTable &categoryTable);
    ~PageTypeArticleHealth() override;

    QString getTypeId()      const override;
    QString getDisplayName() const override;

    /**
     * Renders symptom links between the category breadcrumb (bloc 0) and
     * the article text (bloc 1), without changing the storage key order.
     */
    QList<const AbstractPageBloc *> getRenderBlocs() const override;

    /**
     * Stores the working directory; delegates to bindWorkingDir().
     */
    void bindGenerationContext(IPageRepository &repo, const QDir &workingDir) override;

    /**
     * Passes the working directory to PageBlocSymptomLinks so its edit widget
     * and getAiKeyClues() can load the symptom vocabulary from taxonomy.db.
     * Called by both bindGenerationContext() and GenPageQueue._schema().
     */
    void bindWorkingDir(const QDir &workingDir) override;

private:
    PageBlocSymptomLinks m_symptomLinksBloc;
};

#endif // PAGETYPEARTICLEHEALTH_H
