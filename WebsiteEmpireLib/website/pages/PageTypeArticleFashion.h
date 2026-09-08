#ifndef PAGETYPEARTICLEFASHION_H
#define PAGETYPEARTICLEFASHION_H

#include "website/pages/PageTypeArticleBase.h"
#include "website/pages/blocs/PageBlocFashionTaxonomyLinks.h"

class CategoryTable;

/**
 * Fashion-vertical article page type.
 *
 * Adds one bloc to PageTypeArticleBase's seven generic ones:
 *   7 — PageBlocFashionTaxonomyLinks : editor-selected Color/Season/Occasion/
 *       Material/Style Aesthetic tag links — the Fashion-vertical equivalent
 *       of PageTypeArticleHealth's PageBlocSymptomLinks.
 *
 * Registered in the AbstractPageType registry under TYPE_ID =
 * "article_fashion" — a new id, independent of PageTypeArticleHealth's
 * "article".
 *
 * bindGenerationContext() stores the working directory so that addCode() can
 * supply it to PageBlocFashionTaxonomyLinks for aspire DB queries.
 */
class PageTypeArticleFashion : public PageTypeArticleBase
{
public:
    static constexpr const char *TYPE_ID      = "article_fashion";
    static constexpr const char *DISPLAY_NAME = "Article (Fashion)";

    explicit PageTypeArticleFashion(CategoryTable &categoryTable);
    ~PageTypeArticleFashion() override;

    QString getTypeId()      const override;
    QString getDisplayName() const override;

    /**
     * Stores the working directory; delegates to bindWorkingDir().
     */
    void bindGenerationContext(IPageRepository &repo, const QDir &workingDir) override;

    /**
     * Passes the working directory to PageBlocFashionTaxonomyLinks so its
     * edit widget and getAiKeyClues()-equivalent lookups can load each
     * dimension's vocabulary from taxonomy.db.
     * Called by both bindGenerationContext() and GenPageQueue._schema().
     */
    void bindWorkingDir(const QDir &workingDir) override;

private:
    PageBlocFashionTaxonomyLinks m_fashionTaxonomyLinksBloc;
};

#endif // PAGETYPEARTICLEFASHION_H
