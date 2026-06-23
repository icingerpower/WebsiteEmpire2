#ifndef PAGETYPESYMPTOMINDEX_H
#define PAGETYPESYMPTOMINDEX_H

#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/PageBlocMeta.h"
#include "website/pages/blocs/PageBlocSocial.h"
#include "website/pages/blocs/PageBlocText.h"

#include <QDir>

/**
 * A single page at /symptoms listing all symptoms alphabetically.
 *
 * Permalink: /symptoms
 * Title pattern: "Browse conditions by symptom — 247 symptoms"
 *
 * Blocs (in order):
 *   0 — PageBlocText   : optional AI-generated intro (may be empty)
 *   1 — PageBlocSocial : social-media text metadata
 *   2 — PageBlocMeta   : SEO title + meta description
 *
 * The symptom grid is rendered by addInnerTopCode() BEFORE the text bloc
 * so that the intro paragraph appears below the list.  This ordering
 * places the machine-generated, structured content first — ideal for
 * featured-snippet targeting — and the prose second.
 *
 * Wait — addInnerTopCode is called before all blocs, so actually the
 * symptom list appears at the top inside <main>, followed by the intro text.
 *
 * Registered under TYPE_ID = "symptom_index".
 *
 * bindGenerationContext() stores the working directory for DB access.
 */
class PageTypeSymptomIndex : public AbstractPageType
{
public:
    static constexpr const char *TYPE_ID      = "symptom_index";
    static constexpr const char *DISPLAY_NAME = "Symptom Index";

    explicit PageTypeSymptomIndex(CategoryTable &categoryTable);
    ~PageTypeSymptomIndex() override = default;

    QString getTypeId()      const override;
    QString getDisplayName() const override;

    const QList<const AbstractPageBloc *> &getPageBlocs() const override;

    /**
     * Stores the working directory so that addInnerTopCode() can read the
     * aspire databases for symptom and condition counts.
     */
    void bindGenerationContext(IPageRepository &repo, const QDir &workingDir) override;

    /**
     * Emits:
     *   <title> — from PageBlocMeta or computed "Browse conditions by symptom — N symptoms"
     *   <meta name="description"> — from PageBlocMeta
     *   Canonical, og:url, social, JSON-LD WebPage
     */
    QString buildHeadMetaTags(const QString &baseUrl, const QString &langCode) const override;

    /** Returns the social text bloc for the page generator. */
    const PageBlocSocial &socialTextBloc() const { return m_socialTextBloc; }

    /** Returns the SEO meta bloc for the page generator. */
    const PageBlocMeta &metaBloc() const { return m_metaBloc; }

protected:
    /**
     * Renders the symptom grid: reads all symptom names from
     * PageAttributesHealthSymptom.db, computes per-symptom condition counts
     * from PageAttributesHealthCondition.db, and emits an alphabetical grid
     * linking to /symptoms/<slug>.
     *
     * Called first inside <main> before the text bloc.
     */
    void addInnerTopCode(AbstractEngine &engine,
                         int             websiteIndex,
                         QString        &html,
                         QString        &css,
                         QString        &js,
                         QSet<QString>  &cssDoneIds,
                         QSet<QString>  &jsDoneIds) const override;

private:
    PageBlocText                     m_textBloc;
    PageBlocSocial                   m_socialTextBloc;
    PageBlocMeta                     m_metaBloc;
    QList<const AbstractPageBloc *>  m_blocs;
    QDir                             m_workingDir;
};

#endif // PAGETYPESYMPTOMINDEX_H
