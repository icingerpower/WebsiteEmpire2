#ifndef PAGETYPETAXONOMYINDEX_H
#define PAGETYPETAXONOMYINDEX_H

#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/PageBlocMeta.h"
#include "website/pages/blocs/PageBlocSocial.h"
#include "website/pages/blocs/PageBlocText.h"

#include <QDir>
#include <QStringList>

class IPageRepository;

/**
 * A page (or letter-paginated set of pages) listing all taxonomy and category
 * hub pages alphabetically.
 *
 * Permalink structure:
 *   /categories          — single-page layout (≤ PAGINATION_THRESHOLD entries)
 *                          or the letter-A default (> PAGINATION_THRESHOLD)
 *   /categories/b … /z  — one page per letter (> PAGINATION_THRESHOLD only)
 *
 * The active letter is derived from the trailing path segment of m_permalink:
 *   "/categories"   → default page (renders 'a' when paginated, all when not)
 *   "/categories/b" → letter 'b'
 *
 * When entry count exceeds the threshold an A–Z navigation bar is injected
 * above the grid by addInnerTopCode().
 *
 * Blocs (in order):
 *   0 — PageBlocText   : optional AI-generated intro paragraph
 *   1 — PageBlocSocial : social-media metadata
 *   2 — PageBlocMeta   : SEO title + meta description
 *
 * The taxonomy grid is rendered by addInnerTopCode() BEFORE the blocs, placing
 * the structured content first (good for featured-snippet targeting).
 *
 * Stubs are created and maintained by TaxonomyIndexSyncer.
 * Translation is handled automatically via the inherited bloc machinery.
 *
 * Registered under TYPE_ID = "taxonomy_index".
 */
class PageTypeTaxonomyIndex : public AbstractPageType
{
public:
    static constexpr const char *TYPE_ID       = "taxonomy_index";
    static constexpr const char *DISPLAY_NAME  = "Taxonomy Index";

    /** Base permalink — single-page layout and letter-A default. */
    static constexpr const char *BASE_PERMALINK = "/categories";

    /**
     * Page type IDs whose generated pages are listed in this index.
     * Extend when new taxonomy-like page types are registered.
     */
    static QStringList aggregatedTypeIds();

    explicit PageTypeTaxonomyIndex(CategoryTable &categoryTable);
    ~PageTypeTaxonomyIndex() override;

    QString getTypeId()      const override;
    QString getDisplayName() const override;

    const QList<const AbstractPageBloc *> &getPageBlocs() const override;

    /**
     * Stores the repository reference so that addInnerTopCode() can read
     * generated taxonomy/category pages at render time.
     */
    void bindGenerationContext(IPageRepository &repo, const QDir &workingDir) override;

    /**
     * Emits <title>, <meta description>, canonical, og:url, social tags,
     * and a WebPage JSON-LD snippet.
     * Falls back to a computed title when PageBlocMeta has no stored value.
     */
    QString buildHeadMetaTags(const QString &baseUrl, const QString &langCode) const override;

protected:
    /**
     * Renders the taxonomy grid and, when in paginated mode, an A–Z navigation
     * bar.  Called first inside <main>, before all blocs.
     *
     * Does nothing when m_repo has not been bound or when no source pages are
     * generated yet.
     */
    void addInnerTopCode(AbstractEngine &engine,
                         int             websiteIndex,
                         QString        &html,
                         QString        &css,
                         QString        &js,
                         QSet<QString>  &cssDoneIds,
                         QSet<QString>  &jsDoneIds) const override;

private:
    /**
     * Returns the active letter derived from m_permalink:
     *   "/categories"   → QChar() (null → default page)
     *   "/categories/b" → QChar('b')
     */
    QChar _activeLetter() const;

    /**
     * Converts a permalink slug (e.g. "cooking-recipes" or "cooking-recipes.html")
     * to a title-cased display name ("Cooking Recipes").
     * Strips a trailing ".html" extension before processing.
     */
    static QString _slugToDisplayName(const QString &slug);

    PageBlocText                     m_textBloc;
    PageBlocSocial                   m_socialTextBloc;
    PageBlocMeta                     m_metaBloc;
    QList<const AbstractPageBloc *>  m_blocs;

    IPageRepository *m_repo = nullptr;
};

#endif // PAGETYPETAXONOMYINDEX_H
