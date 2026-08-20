#ifndef PAGETYPESYMPTOMHUB_H
#define PAGETYPESYMPTOMHUB_H

#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/PageBlocConditionList.h"
#include "website/pages/blocs/PageBlocMeta.h"
#include "website/pages/blocs/PageBlocSocial.h"
#include "website/pages/blocs/PageBlocText.h"

/**
 * One page per symptom, auto-generated from the aspire database.
 *
 * Permalink pattern: /symptoms/<slug>
 * Title pattern:     "Difficulty swallowing — 13 possible conditions"
 *
 * Blocs (in order):
 *   0 — PageBlocText           : AI-generated intro paragraph about the symptom
 *   1 — PageBlocConditionList  : auto-rendered list of matching conditions (aspire DB)
 *   2 — PageBlocSocial         : social-media text metadata
 *   3 — PageBlocMeta           : SEO title + meta description
 *
 * Registered under TYPE_ID = "symptom_hub".
 *
 * bindGenerationContext() stores the working directory so that addCode()
 * can supply it to PageBlocConditionList and buildHeadMetaTags().
 *
 * The page title is computed dynamically from the condition count in the
 * aspire database; a PageBlocMeta override takes precedence when non-empty.
 */
class PageTypeSymptomHub : public AbstractPageType
{
public:
    static constexpr const char *TYPE_ID      = "symptom_hub";
    static constexpr const char *DISPLAY_NAME = "Symptom Hub";

    explicit PageTypeSymptomHub(CategoryTable &categoryTable);
    ~PageTypeSymptomHub() override = default;

    QString getTypeId()      const override;
    QString getDisplayName() const override;

    const QList<const AbstractPageBloc *> &getPageBlocs() const override;

    /**
     * Stores the working directory so that addCode() can pass it to
     * PageBlocConditionList for aspire DB queries.
     */
    void bindGenerationContext(IPageRepository &repo, const QDir &workingDir) override;

    /**
     * addCode() override that injects the render context into PageBlocConditionList
     * before delegating to AbstractPageType::addCode().
     *
     * Both m_permalink (set by setGenerationContext) and m_workingDir (set by
     * bindGenerationContext) must be available at the time this is called.
     */
    void addCode(QStringView     origContent,
                 AbstractEngine &engine,
                 int             websiteIndex,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

    /**
     * Adds social meta tags and WebPage JSON-LD on top of the base tags.
     * <title> and <meta name="description"> come from the base via autoSeoTitle /
     * autoSeoDescription.
     */
    QString buildHeadMetaTags(const QString &baseUrl, const QString &langCode, const QString &canonicalPath) const override;

    /**
     * Returns the English SEO templates for symptom hub pages:
     *   "title" → "What Causes %1? %2 Conditions To Know"
     *   "desc"  → "Find out which %1 conditions cause %2 and what biomarkers
     *              help identify the root cause."
     */
    QMap<QString, QString> seoTemplateStrings() const override;

protected:

    /**
     * Returns the stored SEO title (PageBlocMeta) when set; otherwise uses
     * the translated template from seoTemplate("title", langCode).
     */
    QString autoSeoTitle(const QString &langCode) const override;

    /**
     * Returns the stored meta description (PageBlocMeta) when set; otherwise
     * uses the translated template from seoTemplate("desc", langCode).
     */
    QString autoSeoDescription(const QString &langCode) const override;

    /** Returns the social text bloc for the page generator. */
    const PageBlocSocial &socialTextBloc() const { return m_socialTextBloc; }

    /** Returns the SEO meta bloc for the page generator. */
    const PageBlocMeta &metaBloc() const { return m_metaBloc; }

private:
    PageBlocText                     m_textBloc;
    PageBlocConditionList            m_conditionListBloc;
    PageBlocSocial                   m_socialTextBloc;
    PageBlocMeta                     m_metaBloc;
    QList<const AbstractPageBloc *>  m_blocs;
};

#endif // PAGETYPESYMPTOMHUB_H
