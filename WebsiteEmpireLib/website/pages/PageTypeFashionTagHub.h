#ifndef PAGETYPEFASHIONTAGHUB_H
#define PAGETYPEFASHIONTAGHUB_H

#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/PageBlocFashionHubGrid.h"
#include "website/pages/blocs/PageBlocSocial.h"
#include "website/pages/blocs/PageBlocSocialMedia.h"
#include "website/pages/blocs/PageBlocTaxonomyArticle.h"

class CategoryTable;

/**
 * A page type that displays a fashion-tag hub: a CSS Grid of article cards
 * tagged with one fashion taxonomy value (e.g. every article tagged
 * Color=Burgundy at /colors/burgundy).
 *
 * One type covers all dimensions (Color, Season, Occasion, Material, Style
 * Aesthetic, Product Type, Demographic, Fit/Silhouette, Pattern, Culture) —
 * the specific dimension and tag value a given hub page covers are page
 * data (PageBlocFashionHubGrid::KEY_DIMENSION/KEY_TAG_VALUE), not baked into
 * the class, same pattern PageTypeCategory already uses (one type, many
 * categories via stored data) rather than one near-duplicate page-type
 * class per dimension.
 *
 * Blocs (in order):
 *   0 — PageBlocFashionHubGrid : grid of article cards, sorted by CTR -> views -> recency
 *   1 — PageBlocSocial         : social-media text metadata (title + desc, first pass)
 *   2 — PageBlocSocialMedia    : social-media image variants (second pass, opt-in)
 *
 * Registered under TYPE_ID = "fashion_tag_hub".
 *
 * bindGenerationContext() passes the page repository and working directory to
 * PageBlocFashionHubGrid so it can read stats.db during generation. This call
 * is made by PageGenerator::generateAll() after load() and setAuthorLang().
 */
class PageTypeFashionTagHub : public AbstractPageType
{
public:
    static constexpr const char *TYPE_ID      = "fashion_tag_hub";
    static constexpr const char *DISPLAY_NAME = "Fashion Tag Hub";

    /** categoryTable is unused — this type has no category bloc — but is
     * required by the AbstractPageType::Factory signature every registered
     * page type must match (see DECLARE_PAGE_TYPE). */
    explicit PageTypeFashionTagHub(CategoryTable &categoryTable);
    ~PageTypeFashionTagHub() override = default;

    QString getTypeId()      const override;
    QString getDisplayName() const override;

    const QList<const AbstractPageBloc *> &getPageBlocs() const override;
    QList<const AbstractPageBloc *> getRenderBlocs() const override;

    void bindGenerationContext(IPageRepository &repo, const QDir &workingDir) override;

    /** Returns the social text bloc (first-pass titles/descs) for the page generator. */
    const PageBlocSocial &socialTextBloc() const { return m_socialTextBloc; }

    /** Returns the social image bloc (second-pass WebP variants) for the page generator. */
    const PageBlocSocialMedia &socialBloc() const { return m_socialBloc; }

    /** Returns the hub grid bloc — used by PageGenerator to compute the translated permalink. */
    const PageBlocFashionHubGrid &hubGridBloc() const { return m_hubGridBloc; }

    /**
     * Adds social meta tags and WebPage JSON-LD on top of the base tags.
     * <title>, <meta name="description">, canonical, and og:url come from the
     * base via autoSeoTitle / autoSeoDescription.
     */
    QString buildHeadMetaTags(const QString &baseUrl, const QString &langCode, const QString &canonicalPath) const override;

    /**
     * Returns the English SEO templates for fashion tag hub pages:
     *   "title"         -> "%1 %2 Outfit Ideas: %3 To Try"
     *   "title_nocount" -> "%1 %2 Outfit Ideas"
     *   "desc"          -> "Browse %1 outfit ideas featuring %2 %3 — %4 looks to inspire your next fit."
     */
    QMap<QString, QString> seoTemplateStrings() const override;

protected:
    /**
     * Returns "{tagName} {dimension} Outfit Ideas: {N} To Try", falling back
     * to the no-count template when the grid rendered zero articles.
     */
    QString autoSeoTitle(const QString &langCode) const override;

    /** Returns the description template filled with the tag name and article count. */
    QString autoSeoDescription(const QString &langCode) const override;

private:
    // Append at index 3: existing persisted grid/social keys must never move.
    PageBlocTaxonomyArticle          m_articleBloc;
    PageBlocFashionHubGrid           m_hubGridBloc;
    PageBlocSocial                   m_socialTextBloc;
    PageBlocSocialMedia              m_socialBloc;
    QList<const AbstractPageBloc *>  m_blocs;
};

#endif // PAGETYPEFASHIONTAGHUB_H
