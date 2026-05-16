#ifndef PAGETYPECATEGORY_H
#define PAGETYPECATEGORY_H

#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/PageBlocHubGrid.h"
#include "website/pages/blocs/PageBlocSocial.h"
#include "website/pages/blocs/PageBlocSocialMedia.h"

class CategoryTable;

/**
 * A page type that displays a category hub: a CSS Grid of article cards drawn
 * from one or more selected categories.
 *
 * Blocs (in order):
 *   0 — PageBlocHubGrid    : grid of article cards, sorted by CTR → views → recency
 *   1 — PageBlocSocial     : social-media text metadata (title + desc, first pass)
 *   2 — PageBlocSocialMedia : social-media image variants (second pass, opt-in)
 *
 * Registered under TYPE_ID = "category_hub".
 *
 * bindGenerationContext() passes the page repository and working directory to
 * PageBlocHubGrid so it can read stats.db during generation.  This call is
 * made by PageGenerator::generateAll() after load() and setAuthorLang().
 */
class PageTypeCategory : public AbstractPageType
{
public:
    static constexpr const char *TYPE_ID      = "category_hub";
    static constexpr const char *DISPLAY_NAME = "Category Hub";

    explicit PageTypeCategory(CategoryTable &categoryTable);
    ~PageTypeCategory() override = default;

    QString getTypeId()      const override;
    QString getDisplayName() const override;

    const QList<const AbstractPageBloc *> &getPageBlocs() const override;

    void bindGenerationContext(IPageRepository &repo, const QDir &workingDir) override;

    /** Returns the social text bloc (first-pass titles/descs) for the page generator. */
    const PageBlocSocial &socialTextBloc() const { return m_socialTextBloc; }

    /** Returns the social image bloc (second-pass WebP variants) for the page generator. */
    const PageBlocSocialMedia &socialBloc() const { return m_socialBloc; }

    QString buildHeadMetaTags(const QString &baseUrl) const override;

private:
    PageBlocHubGrid                 m_hubGridBloc;
    PageBlocSocial                  m_socialTextBloc;
    PageBlocSocialMedia             m_socialBloc;
    QList<const AbstractPageBloc *> m_blocs;
};

#endif // PAGETYPECATEGORY_H
