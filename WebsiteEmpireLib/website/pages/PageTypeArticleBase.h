#ifndef PAGETYPEARTICLEBASE_H
#define PAGETYPEARTICLEBASE_H

#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/PageBlocAutoLink.h"
#include "website/pages/blocs/PageBlocCategoryLinks.h"
#include "website/pages/blocs/PageBlocMeta.h"
#include "website/pages/blocs/PageBlocSocial.h"
#include "website/pages/blocs/PageBlocSocialMedia.h"
#include "website/pages/blocs/PageBlocText.h"

#include <QDir>
#include <QScopedPointer>

class CategoryTable;
class PageBlocCategory;

/**
 * Shared base for article-style page types across verticals (Health,
 * Fashion, ...).
 *
 * Composes seven generic blocs (storage order):
 *   0 — PageBlocCategory      : primary breadcrumb category
 *   1 — PageBlocText          : main article body
 *   2 — PageBlocSocial        : social-media text metadata (title + desc, first pass)
 *   3 — PageBlocAutoLink      : keywords that auto-link to this page
 *   4 — PageBlocCategoryLinks : cross-reference category links (body parts, etc.)
 *   5 — PageBlocSocialMedia   : social-media image variants (second pass, opt-in)
 *   6 — PageBlocMeta          : SEO title + meta description (translatable)
 *
 * NOT registered via DECLARE_PAGE_TYPE: getTypeId()/getDisplayName() stay
 * pure virtual (inherited from AbstractPageType), so this class can never be
 * constructed through AbstractPageType::createForTypeId() — it exists purely
 * as a shared base for concrete verticals.
 *
 * A concrete vertical (see PageTypeArticleHealth) supplies TYPE_ID/
 * DISPLAY_NAME and may, in its own constructor (after this base's
 * constructor has populated blocs 0–6 into the protected m_blocs list),
 * append its own additional blocs — e.g. PageTypeArticleHealth appends a
 * PageBlocSymptomLinks as bloc 7 and overrides getRenderBlocs() to
 * reposition it in the rendered output without changing storage order.
 * A vertical with no extra blocs (e.g. PageTypeArticleFashion) needs no
 * override at all: it inherits getPageBlocs()/getRenderBlocs() unchanged.
 *
 * The category bloc is first so getAttributes() returns the page's selected
 * categories before any text-bloc attributes.  PageBlocMeta is last so it can
 * be appended without re-indexing the existing 0–5 keys in stored page data.
 *
 * PageBlocCategory is a QObject and is therefore heap-allocated; the
 * QScopedPointer owns it.  The destructor is declared here and defined in the
 * .cpp so that QScopedPointer can see the full PageBlocCategory type at the
 * point of deletion without pulling it into this header.
 *
 * Call setPageUrl() whenever the parent page's URL is known or changes so
 * that PageBlocAutoLink can write the correct key into LinksManager on save.
 *
 * Call setGenerationContext() (AbstractPageType) before addCode() so that
 * buildHeadMetaTags() can emit correct canonical, og:url and hreflang tags.
 */
class PageTypeArticleBase : public AbstractPageType
{
public:
    explicit PageTypeArticleBase(CategoryTable &categoryTable);
    ~PageTypeArticleBase() override;

    const QList<const AbstractPageBloc *> &getPageBlocs() const override;

    bool isCountedInTranslationStats() const override;

    /**
     * Sets the canonical URL of the article page so PageBlocAutoLink can
     * register its keywords under the correct key in LinksManager.
     * Must be called before the first save() for a given page record.
     */
    void setPageUrl(const QString &url);

    /** Returns the social text bloc (first-pass titles/descs) for the page generator. */
    const PageBlocSocial &socialTextBloc() const;

    /** Returns the social image bloc (second-pass WebP variants) for the page generator. */
    const PageBlocSocialMedia &socialBloc() const;

    /** Returns the auto-link bloc for direct access by the page generator. */
    const PageBlocAutoLink &autoLinkBloc() const;

    /** Returns the SEO meta bloc for direct access by tests and the page generator. */
    const PageBlocMeta &metaBloc() const;

    /**
     * Emits into the page <head>:
     *   <title>, <meta name="description">  — from PageBlocMeta (translated)
     *   <link rel="canonical">              — baseUrl + m_permalink
     *   og:url, og:type, og:locale         — derived automatically
     *   <link rel="alternate" hreflang>     — source + each target lang
     *   per-platform social <meta> tags     — from PageBlocSocial + AbstractSocialMedia
     *
     * Requires setGenerationContext() to have been called for canonical /
     * hreflang output; tags that depend on missing data are silently omitted.
     */
    QString buildHeadMetaTags(const QString &baseUrl,
                               const QString &langCode,
                               const QString &canonicalPath) const override;

    /**
     * Rasterizes the article's primary SVG illustration to a 1200×630 WebP and
     * caches it in images.db under domain="" when no second-pass social-media
     * images have been generated.  Sets m_jsonLdFallbackImage on success.
     * No-op when any social image variant is already present.
     */
    void prepareJsonLdImage(const QDir &workingDir, const QString &domain) override;

    /**
     * Returns true: every article vertical always requires an SVG image in
     * the first pass.  LauncherGeneration will not mark the page Complete if
     * SVG generation failed — it stays ContentReady for retry on the next run.
     */
    bool hasSvg() const override;

protected:
    /** Returns the AI-generated SEO title from PageBlocMeta. */
    QString autoSeoTitle(const QString &langCode) const override;

    /** Returns the AI-generated meta description from PageBlocMeta. */
    QString autoSeoDescription(const QString &langCode) const override;

    /**
     * Renders article-specific top content (AI disclaimer, date line) inside
     * <main> before the blocs by calling AbstractTheme::addCodeArticle().
     * Always calls AbstractPageType::addInnerTopCode() first.
     */
    void addInnerTopCode(AbstractEngine &engine,
                         int             websiteIndex,
                         QString        &html,
                         QString        &css,
                         QString        &js,
                         QSet<QString>  &cssDoneIds,
                         QSet<QString>  &jsDoneIds) const override;

    QScopedPointer<PageBlocCategory> m_categoryBloc;
    PageBlocText                     m_textBloc;
    PageBlocSocial                   m_socialTextBloc;
    PageBlocAutoLink                 m_autoLinkBloc;
    PageBlocCategoryLinks            m_categoryLinksBloc;
    PageBlocSocialMedia              m_socialBloc;
    PageBlocMeta                     m_metaBloc;
    QList<const AbstractPageBloc *>  m_blocs;
};

#endif // PAGETYPEARTICLEBASE_H
