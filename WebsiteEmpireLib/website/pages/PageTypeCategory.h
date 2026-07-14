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

    /**
     * Adds social meta tags and WebPage JSON-LD on top of the base tags.
     * <title>, <meta name="description">, canonical, and og:url come from the
     * base via autoSeoTitle / autoSeoDescription / autoH1.
     */
    QString buildHeadMetaTags(const QString &baseUrl, const QString &langCode) const override;

    /**
     * Returns the English SEO templates for category hub pages:
     *   "title"         → "%1 Health: %2 Conditions to Track"
     *   "title_nocount" → "%1 Health: Conditions to Track"
     *   "desc"          → "%1 %2 conditions, each mapped to the genes and
     *                      biomarkers that explain individual risk and response."
     *   "h1"            → "%1 Health"
     */
    QMap<QString, QString> seoTemplateStrings() const override;

protected:

    /**
     * Returns: "{CategoryName} Health: {N} Conditions to Track"
     * N is the article count from the last addCode() call.
     * Falls back to "{CategoryName} Health: Conditions to Track" when N = 0.
     */
    QString autoSeoTitle(const QString &langCode) const override;

    /**
     * Returns: "{N} {categoryName} conditions, each mapped to the genes and
     * biomarkers that explain individual risk and response."
     */
    QString autoSeoDescription(const QString &langCode) const override;

    /**
     * Returns: "{CategoryName} Health" — emitted as <h1> before the grid.
     * Called from addInnerTopCode(), which runs BEFORE blocs, so no article
     * count is available here (intentional — H1 does not include the count).
     */
    QString autoH1(const QString &langCode) const override;

private:
    PageBlocHubGrid                 m_hubGridBloc;
    PageBlocSocial                  m_socialTextBloc;
    PageBlocSocialMedia             m_socialBloc;
    QList<const AbstractPageBloc *> m_blocs;
};

#endif // PAGETYPECATEGORY_H
