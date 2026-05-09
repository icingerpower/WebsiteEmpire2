#ifndef DIALOGPICKARTICLESTORESET_H
#define DIALOGPICKARTICLESTORESET_H

#include <QDialog>
#include <QList>

struct PageRecord;

namespace Ui { class DialogPickArticlesToReset; }

/**
 * Shows all pages that have update attempt records for a given prompt.
 * All items start checked; the user can uncheck those they want to keep.
 * selectedPageIds() returns the IDs of checked items after accept().
 */
class DialogPickArticlesToReset : public QDialog
{
    Q_OBJECT
public:
    explicit DialogPickArticlesToReset(const QList<PageRecord> &pages,
                                        QWidget *parent = nullptr);
    ~DialogPickArticlesToReset();

    QList<int> selectedPageIds() const;

private slots:
    void _checkAll();
    void _uncheckAll();

private:
    Ui::DialogPickArticlesToReset *ui;
    QList<int>                     m_pageIds;
};

#endif // DIALOGPICKARTICLESTORESET_H
