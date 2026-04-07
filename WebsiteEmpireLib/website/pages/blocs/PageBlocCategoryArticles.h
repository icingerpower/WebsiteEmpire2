#ifndef PAGEBLOCCATEGORYARTICLES_H
#define PAGEBLOCCATEGORYARTICLES_H

#include "website/pages/blocs/AbstractPageBloc.h"

#include <QList>
#include <QString>

class CategoryTable;
class IPageRepository;

/**
 * A page bloc that displays per-category article lists side-by-side.
 *
 * Each configured entry maps a category id to a display heading, a maximum
 * article count, and a sort order (recent, performance, or combined).
 *
 * addCode() queries the page repository for all source articles, groups them
 * by category, sorts each group according to the entry's sort order, and emits
 * a responsive flexbox grid of link lists.
 *
 * CSS and JS are appended once per page via cssDoneIds / jsDoneIds with the
 * shared ID "page-bloc-category-articles".
 *
 * JS uses IntersectionObserver for section display tracking and a click
 * listener for link click tracking, both via navigator.sendBeacon('/stats').
 *
 * The CategoryTable and IPageRepository references must outlive this bloc.
 */
class PageBlocCategoryArticles : public AbstractPageBloc
{
public:
    // ── Sort constants ─────────────────────────────────────────────────
    static constexpr const char *SORT_RECENT      = "recent";
    static constexpr const char *SORT_PERFORMANCE  = "performance";
    static constexpr const char *SORT_COMBINED     = "combined";

    // ── Serialisation keys ─────────────────────────────────────────────
    static constexpr const char *KEY_ENTRY_COUNT   = "entry_count";

    // ── Entry struct ───────────────────────────────────────────────────
    struct CategoryEntry {
        int     categoryId  = 0;
        QString title;           ///< display heading (may differ from category name)
        int     itemCount   = 5; ///< max articles to show
        QString sortOrder;       ///< one of SORT_* constants
    };

    explicit PageBlocCategoryArticles(CategoryTable &categoryTable,
                                      IPageRepository &repo);
    ~PageBlocCategoryArticles() override = default;

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

    // ── Accessors (for tests) ──────────────────────────────────────────
    const QList<CategoryEntry> &entries() const;

private:
    /**
     * Derives a human-readable title from a permalink.
     *   /my-yoga-article.html  ->  My Yoga Article
     */
    static QString permalinkToTitle(const QString &permalink);

    CategoryTable   &m_categoryTable;
    IPageRepository &m_repo;
    QList<CategoryEntry> m_entries;
};

#endif // PAGEBLOCCATEGORYARTICLES_H
