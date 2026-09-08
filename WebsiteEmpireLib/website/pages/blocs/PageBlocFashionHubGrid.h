#ifndef PAGEBLOCFASHIONHUBGRID_H
#define PAGEBLOCFASHIONHUBGRID_H

#include "website/pages/blocs/AbstractPageBloc.h"

#include <QDir>
#include <QHash>
#include <QString>

class IPageRepository;

/**
 * A page bloc that renders a responsive CSS Grid of article cards tagged
 * with one fashion taxonomy value (e.g. every article tagged Color=Burgundy).
 *
 * Generic sibling of PageBlocHubGrid: same CTR -> views -> recency stats
 * sort and CSS grid render, but membership is "articles whose
 * PageBlocFashionTaxonomyLinks selection for KEY_DIMENSION includes
 * KEY_TAG_VALUE" instead of category ids — PageBlocHubGrid is tightly
 * coupled to CategoryTable/category ids so it isn't reused directly.
 *
 * dimension/tagValue are set once by FashionTaxonomyHubSyncer::syncStubs()
 * when the stub hub page is created and never edited afterward — there is
 * no picker UI, unlike PageBlocHubGrid's category checkboxes.
 *
 * bindContext() must be called before addCode() is invoked during
 * generation. Without it, addCode() is a no-op (safe for widget preview
 * paths that never call addCode).
 */
class PageBlocFashionHubGrid : public AbstractPageBloc
{
public:
    static constexpr const char *KEY_DIMENSION = "dimension";
    static constexpr const char *KEY_TAG_VALUE = "tag_value";
    static constexpr int         MAX_ARTICLES  = 12;

    PageBlocFashionHubGrid() = default;
    ~PageBlocFashionHubGrid() override = default;

    /**
     * Injects the page repository and working directory needed by addCode().
     * Called by PageTypeFashionTagHub::bindGenerationContext().
     */
    void bindContext(IPageRepository &repo, const QDir &workingDir);

    QString getName() const override;

    void load(const QHash<QString, QString> &values) override;
    void save(QHash<QString, QString> &values) const override;

    void addCode(QStringView     origContent,
                 AbstractEngine &engine,
                 int             websiteIndex,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

    AbstractPageBlockWidget *createEditWidget() override;

    /** The taxonomyId this hub covers (e.g. "fashion_color"), empty if unset. */
    QString dimension() const { return m_dimension; }

    /** The tag value this hub covers (e.g. "Burgundy"), empty if unset. */
    QString tagValue() const { return m_tagValue; }

    /**
     * Returns the translated display name of the tag value for langCode,
     * falling back to the English tagValue(). Empty when dimension/tagValue
     * are unset. Callable before addCode() (loaded by load()).
     */
    QString translatedTagName(const QString &langCode) const;

    /**
     * Returns the number of articles rendered in the last addCode() call.
     * Zero before any call or when no articles matched. Used by
     * PageTypeFashionTagHub::autoSeoTitle/Description(), always called
     * after addCode() via buildHeadMetaTags().
     */
    int lastRenderedCount() const;

private:
    struct ArticleStats {
        double ctr   = 0.0;
        int    views = 0;
    };

    /**
     * Opens stats.db and reads CTR (from displays_clicks where page_id LIKE
     * 'hub:%') and view counts (from page_session). Mirrors
     * PageBlocHubGrid::_loadStats() exactly.
     * Returns empty map if stats.db does not exist or cannot be opened.
     */
    QHash<QString, ArticleStats> _loadStats() const;

    QString          m_dimension;
    QString          m_tagValue;
    IPageRepository *m_repo = nullptr;
    QDir             m_workingDir;

    // Set at the end of addCode(); consumed by lastRenderedCount().
    mutable int m_lastRenderedCount = 0;
};

#endif // PAGEBLOCFASHIONHUBGRID_H
