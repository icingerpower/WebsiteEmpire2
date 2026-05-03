#ifndef PAGEBLOCHUBGRID_H
#define PAGEBLOCHUBGRID_H

#include "website/pages/blocs/AbstractPageBloc.h"

#include <QDir>
#include <QHash>
#include <QList>
#include <QString>

class CategoryTable;
class IPageRepository;

/**
 * A page bloc that renders a responsive CSS Grid of article cards.
 *
 * Articles are filtered by one or more selected category IDs, then sorted by:
 *   1. CTR descending  (clicks / displays from hub tracking in stats.db)
 *   2. View count descending  (page_session rows in stats.db)
 *   3. Most recently updated  (updatedAt from PageRecord)
 *
 * Stats are read from stats.db in the working directory when that directory
 * is available.  If no stats exist yet (fresh install), the bloc falls back
 * gracefully to view-count → recency ordering.
 *
 * Hub display/click tracking uses page_id = "hub:<permalink>" so that it is
 * separate from the organic page_session data.
 *
 * bindContext() must be called by PageTypeCategory::bindGenerationContext()
 * before addCode() is invoked during generation.  Without it, addCode() is a
 * no-op (safe for widget preview paths that never call addCode).
 *
 * The CategoryTable reference must outlive this bloc.
 */
class PageBlocHubGrid : public AbstractPageBloc
{
public:
    static constexpr const char *KEY_CATEGORIES   = "categories";
    static constexpr const char *KEY_MAX_ARTICLES = "max_articles";

    explicit PageBlocHubGrid(CategoryTable &categoryTable);
    ~PageBlocHubGrid() override = default;

    /**
     * Injects the page repository and working directory needed by addCode().
     * Called by PageTypeCategory::bindGenerationContext().
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

private:
    struct ArticleStats {
        double ctr   = 0.0;
        int    views = 0;
    };

    /**
     * Opens stats.db and reads CTR (from displays_clicks where page_id LIKE
     * 'hub:%') and view counts (from page_session).
     * Returns empty map if stats.db does not exist or cannot be opened.
     */
    QHash<QString, ArticleStats> _loadStats() const;

    CategoryTable   &m_categoryTable;
    IPageRepository *m_repo = nullptr;
    QDir             m_workingDir;

    QList<int> m_selectedCategoryIds;
    int        m_maxArticles = 12;
};

#endif // PAGEBLOCHUBGRID_H
