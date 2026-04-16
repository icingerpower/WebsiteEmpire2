#ifndef PAGEBLOC_AUTOLINK_WIDGET_H
#define PAGEBLOC_AUTOLINK_WIDGET_H

#include "AbstractPageBlockWidget.h"

namespace Ui { class PageBlocAutoLinkWidget; }

/**
 * Edit widget for a PageBlocAutoLink bloc.
 *
 * Presents an editable list of keywords that trigger automatic in-text links
 * to the parent article page.
 *
 * - "Add"    appends a blank row and starts inline editing immediately.
 * - "Remove" deletes the currently selected row.
 * - Double-click or F2 edits any row inline.
 *
 * The page URL is round-tripped through the hash (KEY_PAGE_URL) so that
 * save() can write it back without the widget needing to know where the page
 * lives.  It is never displayed or editable here.
 */
class PageBlocAutoLinkWidget : public AbstractPageBlockWidget
{
    Q_OBJECT

public:
    explicit PageBlocAutoLinkWidget(QWidget *parent = nullptr);
    ~PageBlocAutoLinkWidget() override;

    void load(const QHash<QString, QString> &values) override;
    void save(QHash<QString, QString> &values) const override;

private:
    Ui::PageBlocAutoLinkWidget *ui;
    QString m_pageUrl;   // round-tripped from load() → save() unchanged
};

#endif // PAGEBLOC_AUTOLINK_WIDGET_H
