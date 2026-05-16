#ifndef DIALOGPICKPAGE_H
#define DIALOGPICKPAGE_H

#include <QDialog>

namespace Ui { class DialogPickPage; }

class PageFilterProxy;
class QComboBox;
class QStandardItemModel;

/**
 * Modal dialog for picking an existing or AI-generated page permalink.
 *
 * Opens pages.db from the current working directory (WorkingDirectoryManager).
 * If pages.db does not exist yet, both lists are empty.
 *
 * Source pages (sourcePageId == 0) with an empty generatedAt appear in
 * the "Existing pages" toolbox pane; those with a non-empty generatedAt
 * appear in the "Generated pages" pane.  Both panes support filtering by
 * page type and by permalink substring.
 *
 * Usage:
 *   DialogPickPage dlg(this);
 *   if (dlg.exec() == QDialog::Accepted) {
 *       const QString url = dlg.selectedPermalink();
 *   }
 */
class DialogPickPage : public QDialog
{
    Q_OBJECT

public:
    explicit DialogPickPage(QWidget *parent = nullptr);
    ~DialogPickPage() override;

    /** Returns the permalink of the row that was selected when OK was clicked. */
    QString selectedPermalink() const;

    void accept() override;

private:
    void loadPages();
    void populateTypeCombo(QComboBox *combo, QStandardItemModel *model);
    void updateOkButton();

    Ui::DialogPickPage  *ui;
    QStandardItemModel  *m_existingModel  = nullptr;
    QStandardItemModel  *m_generatedModel = nullptr;
    PageFilterProxy     *m_existingProxy  = nullptr;
    PageFilterProxy     *m_generatedProxy = nullptr;
    QString              m_selectedPermalink;
};

#endif // DIALOGPICKPAGE_H
