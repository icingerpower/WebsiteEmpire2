#ifndef PAGEBLOCONDITIONLIST_H
#define PAGEBLOCONDITIONLIST_H

#include "website/pages/blocs/AbstractPageBloc.h"

#include <QDir>
#include <QHash>
#include <QString>

/**
 * Page bloc for symptom hub pages.
 *
 * Given the current page's permalink (/symptoms/<slug>), resolves the canonical
 * symptom name from PageAttributesHealthSymptom.db, then queries
 * PageAttributesHealthCondition.db for every condition that lists that symptom.
 *
 * For each condition it renders an <li> with a link to the condition article
 * page.  Article permalinks are located by querying pages.db directly, which
 * is the only reliable way to handle endPermalink suffixes (e.g. a condition
 * "Brucellosis" with endPermalink "genes-biomarkers" has permalink
 * "/brucellosis-genes-biomarkers", not "/brucellosis").
 *
 * This bloc stores no page_data.  Call setRenderContext() from
 * PageTypeSymptomHub::addCode() before this bloc's addCode() runs.
 *
 * countConditions() is provided for PageTypeSymptomHub::buildHeadMetaTags()
 * so that the page title ("Fatigue — 42 possible conditions") is correct
 * without a second full DB scan.
 */
class PageBlocConditionList : public AbstractPageBloc
{
public:
    ~PageBlocConditionList() override = default;

    /**
     * Injects the page permalink and working directory.
     * Declared const so the page type's const addCode() override can call it.
     */
    void setRenderContext(const QString &permalink, const QDir &workingDir) const;

    /**
     * Queries PageAttributesHealthCondition.db and returns the number of
     * conditions that carry the current page's symptom.
     * Returns 0 when the render context has not been set.
     */
    int countConditions() const;

    QString getName() const override;

    /** No-op: this bloc carries no stored page_data. */
    void load(const QHash<QString, QString> &values) override;

    /** No-op: this bloc carries no stored page_data. */
    void save(QHash<QString, QString> &values) const override;

    /**
     * Renders an ordered list of condition names as links to their article
     * pages.  Conditions are listed in the order they appear in the aspire DB.
     * A "Back to all symptoms" link is appended at the bottom.
     */
    void addCode(QStringView     origContent,
                 AbstractEngine &engine,
                 int             websiteIndex,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

    AbstractPageBlockWidget *createEditWidget() override;

private:
    /**
     * Looks up the canonical symptom name from the slug derived from
     * m_permalink.  Returns empty string when not found.
     */
    QString _resolveSymptomName() const;

    /**
     * Queries PageAttributesHealthCondition.db and returns every condition
     * name that lists symptomName in its health_condition_symptoms field.
     */
    QStringList _loadConditionNames(const QString &symptomName) const;

    /**
     * Searches pages.db for an article page whose permalink starts with
     * '/' + conditionSlug (to handle endPermalink suffixes).
     * Returns the found permalink, or empty if none.
     */
    QString _findArticlePermalink(const QString &conditionSlug) const;

    /**
     * Queries pages.db for all article pages that have a symptom whose slug
     * matches the given slug.  Slugification uses SymptomNav::slugify so
     * matching is consistent with the rest of the symptom navigation system.
     *
     * Works even when the symptom is absent from PageAttributesHealthSymptom.db
     * (the aspire database), which is the common case for newly-assigned symptoms.
     */
    QStringList _loadArticlesForSymptomSlug(const QString &slug) const;

    /**
     * Batch-loads the body text (key "1_text") for the given article permalinks
     * from pages.db.  Returns a map from permalink → text; absent entries mean
     * the article has no text stored yet.
     */
    QHash<QString, QString> _loadArticleTexts(const QStringList &permalinks) const;

    mutable QString m_permalink;
    mutable QDir    m_workingDir;
    mutable bool    m_contextBound = false;
};

#endif // PAGEBLOCONDITIONLIST_H
