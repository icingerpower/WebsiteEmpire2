#ifndef PAGEBLOCCATEGORY_H
#define PAGEBLOCCATEGORY_H

#include "website/pages/blocs/AbstractPageBloc.h"
#include "website/pages/attributes/AttributeCategory.h"

#include <QList>
#include <QObject>

class CategoryTable;

/**
 * A page bloc that tags a page with one or more categories.
 *
 * Content format: comma-separated integer category ids, e.g. "1,3,5".
 * addCode() renders the selected categories as an HTML list.
 *
 * getAttributes() returns AttributeCategory instances for the currently
 * loaded content.  Call setContent() before getAttributes() so the bloc
 * reflects the page's actual selections.
 *
 * When categories are deleted from the CategoryTable this bloc automatically
 * removes the stale ids from its selection and emits contentChanged() with
 * the cleaned content so the page storage layer can re-save the article.
 *
 * QObject must be listed first so that the Qt meta-object system resolves
 * correctly under multiple inheritance.
 *
 * The CategoryTable reference must outlive this bloc.
 */
class PageBlocCategory : public QObject, public AbstractPageBloc
{
    Q_OBJECT

public:
    explicit PageBlocCategory(CategoryTable &table, QObject *parent = nullptr);
    ~PageBlocCategory() override = default;

    // Sets the current content (comma-separated category ids) and rebuilds
    // the attribute list returned by getAttributes().
    void setContent(const QString &content);

    // WebCodeAdder interface.
    // Renders selected categories as <ul class="categories"><li>…</li></ul>.
    // The CSS block is emitted once per page via cssDoneIds.
    void addCode(QStringView    origContent,
                 QString       &html,
                 QString       &css,
                 QString       &js,
                 QSet<QString> &cssDoneIds,
                 QSet<QString> &jsDoneIds) const override;

    // Returns a CategoryEditorWidget in selection mode; ownership is
    // transferred to the caller.
    AbstractPageBlockWidget *createEditWidget() override;

    // Returns AttributeCategory instances for the currently loaded content.
    const QList<const AbstractAttribute *> &getAttributes() const override;

signals:
    // Emitted when a CategoryTable deletion makes the current content stale.
    // The new content string has the deleted ids removed and is ready to be
    // persisted by the page storage layer.
    void contentChanged(const QString &newContent);

private slots:
    void _onCategoriesRemoved(const QList<int> &removedIds);

private:
    void _rebuildAttributes();
    // Parses content and returns the list of valid category ids.
    static QList<int> _parseIds(const QString &content);

    CategoryTable &m_table;
    QList<int>     m_selectedIds;

    // m_categoryStore must be fully built before m_attributes is populated,
    // so that the pointers stored in m_attributes remain stable.
    QList<AttributeCategory>          m_categoryStore;
    QList<const AbstractAttribute *>  m_attributes;
};

#endif // PAGEBLOCCATEGORY_H
