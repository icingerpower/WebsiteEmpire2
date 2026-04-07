#ifndef PAGEBLOCCATEGORYARTICLESWIDGET_H
#define PAGEBLOCCATEGORYARTICLESWIDGET_H

#include "AbstractPageBlockWidget.h"

class CategoryTable;
namespace Ui { class PageBlocCategoryArticlesWidget; }

/**
 * Edit widget for a PageBlocCategoryArticles bloc.
 *
 * Presents a table with one row per category entry (category combo, title,
 * article count spin box, sort combo) and Add/Remove buttons.
 *
 * The CategoryTable reference is used to populate the category combo boxes
 * and must outlive this widget.
 */
class PageBlocCategoryArticlesWidget : public AbstractPageBlockWidget
{
    Q_OBJECT

public:
    explicit PageBlocCategoryArticlesWidget(CategoryTable &categoryTable,
                                            QWidget *parent = nullptr);
    ~PageBlocCategoryArticlesWidget() override;

    void load(const QHash<QString, QString> &values) override;
    void save(QHash<QString, QString> &values) const override;

private:
    Ui::PageBlocCategoryArticlesWidget *ui;
    CategoryTable &m_categoryTable;

    /**
     * Appends a new row to the table with default values.
     * Returns the index of the new row.
     */
    int addRow();
};

#endif // PAGEBLOCCATEGORYARTICLESWIDGET_H
