#ifndef DIALOGGENERATELLEGALPAGES_H
#define DIALOGGENERATELLEGALPAGES_H

#include <QDialog>
#include <QList>
#include <QSet>
#include <QString>

namespace Ui { class DialogGenerateLegalPages; }

/**
 * Lets the user select which legal pages to generate or regenerate.
 *
 * Rows are populated from AbstractLegalPageDef::allDefs().
 *   - Missing pages (not in existingDefIds) are pre-checked: will be created.
 *   - Existing pages are pre-unchecked: the user must explicitly opt in to
 *     regenerate (update permalink to default + re-stamp the def marker;
 *     existing content is preserved).
 *
 * After exec() returns QDialog::Accepted, call selectedDefIds() to retrieve
 * the stable IDs of the pages the user wants to generate.
 *
 * existingDefIds contains AbstractLegalPageDef::getId() values for pages
 * that already exist in the repository for the current editing language.
 */
class DialogGenerateLegalPages : public QDialog
{
    Q_OBJECT

public:
    explicit DialogGenerateLegalPages(const QSet<QString> &existingDefIds,
                                      QWidget             *parent = nullptr);
    ~DialogGenerateLegalPages() override;

    /** Returns the def IDs selected by the user. Valid only after Accepted. */
    QList<QString> selectedDefIds() const;

private:
    Ui::DialogGenerateLegalPages *ui;
};

#endif // DIALOGGENERATELLEGALPAGES_H
