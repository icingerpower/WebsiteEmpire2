#ifndef PAGEBLOCCATEGORYLINKS_H
#define PAGEBLOCCATEGORYLINKS_H

#include "website/pages/blocs/AbstractPageBloc.h"

#include <QList>

class CategoryTable;

/**
 * Page bloc for cross-reference category links displayed beneath the breadcrumb.
 *
 * Stores a set of secondary category IDs (body parts, organs, related
 * attributes, etc.) and renders them grouped by their parent category, e.g.:
 *
 *   Body Parts:  Knee · Shoulder
 *   Bones:       Fibula
 *
 * Each item links to the corresponding hub page when engine.isPageAvailable()
 * confirms that the page exists.  The permalink follows the same slug convention
 * as PageBlocCategory: /<lowercased-hyphenated-name>.html.
 *
 * Content format: comma-separated integer category IDs.  The key name
 * KEY_CATEGORIES ("categories") is intentionally the same as PageBlocCategory,
 * but the two blocs reside at different indices so their stored keys never
 * collide (e.g. "0_categories" vs "4_categories").
 *
 * getAiKeyClues() advertises the available vocabulary inline in the AI schema
 * prompt, clearly distinguishing this from the primary breadcrumb categories.
 *
 * The CategoryTable reference must outlive this bloc.
 */
class PageBlocCategoryLinks : public AbstractPageBloc
{
public:
    static constexpr const char *KEY_CATEGORIES = "categories";

    explicit PageBlocCategoryLinks(CategoryTable &table);
    ~PageBlocCategoryLinks() override = default;

    QString getName() const override;

    /**
     * Returns a hint listing every available category id=name pair with a
     * description that distinguishes these cross-reference links from the
     * primary breadcrumb category.  Empty when no categories exist.
     */
    QHash<QString, QString> getAiKeyClues() const override;

    void load(const QHash<QString, QString> &values) override;

    /** Writes the current selected ids as a sorted comma-separated string. */
    void save(QHash<QString, QString> &values) const override;

    /**
     * Renders a cross-reference section grouped by parent category name.
     * Root-level categories (parentId == 0) are rendered without a group label.
     * CSS is emitted once per page via cssDoneIds.
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
    static QList<int> _parseIds(const QString &content);

    CategoryTable &m_table;
    QList<int>     m_selectedIds;
};

#endif // PAGEBLOCCATEGORYLINKS_H
