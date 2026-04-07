#ifndef PAGEBLOCIMAGELINKSWIDGET_H
#define PAGEBLOCIMAGELINKSWIDGET_H

#include "AbstractPageBlockWidget.h"

namespace Ui { class PageBlocImageLinksWidget; }

/**
 * Edit widget for a PageBlocImageLinks bloc.
 *
 * Presents spinboxes for responsive grid settings (columns/rows per breakpoint)
 * and a table for editing the image-link items (image URL, link type, link
 * target, alt text).
 *
 * The "Link Type" column uses a QComboBox per row (Category / Page / URL).
 *
 * load() reads grid settings and items from the hash; save() writes them back.
 */
class PageBlocImageLinksWidget : public AbstractPageBlockWidget
{
    Q_OBJECT

public:
    explicit PageBlocImageLinksWidget(QWidget *parent = nullptr);
    ~PageBlocImageLinksWidget() override;

    void load(const QHash<QString, QString> &values) override;
    void save(QHash<QString, QString> &values) const override;

private:
    Ui::PageBlocImageLinksWidget *ui;

    /**
     * Appends a new row to the table with an empty image URL, a QComboBox
     * for link type (defaulting to "Category"), empty link target and alt text.
     * Returns the index of the new row.
     */
    int addRow();
};

#endif // PAGEBLOCIMAGELINKSWIDGET_H
