#ifndef DIALOGPICKARTICLESTOUPDATE_H
#define DIALOGPICKARTICLESTOUPDATE_H

#include <QDialog>
#include <QList>

struct PageRecord;

namespace Ui { class DialogPickArticlesToUpdate; }

/**
 * Shows all pages that are candidates for update by a given prompt, sorted by
 * page ID ascending (same order as PanePages).  All items start unchecked; the
 * user checks those they want to update.  A filter field narrows the list by
 * permalink substring.  Check All / Uncheck All apply only to visible rows.
 * selectedPageIds() returns the IDs of all checked items after accept().
 */
class DialogPickArticlesToUpdate : public QDialog
{
    Q_OBJECT
public:
    explicit DialogPickArticlesToUpdate(const QList<PageRecord> &pages,
                                        QWidget *parent = nullptr);
    ~DialogPickArticlesToUpdate();

    QList<int> selectedPageIds() const;

private slots:
    void _checkAll();
    void _uncheckAll();
    void _applyFilter(const QString &text);

private:
    Ui::DialogPickArticlesToUpdate *ui;
};

#endif // DIALOGPICKARTICLESTOUPDATE_H
