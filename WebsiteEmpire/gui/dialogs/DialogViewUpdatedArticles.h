#ifndef DIALOGVIEWUPDATEDARTICLES_H
#define DIALOGVIEWUPDATEDARTICLES_H

#include <QDialog>
#include <QList>

struct PageRecord;

namespace Ui { class DialogViewUpdatedArticles; }

/**
 * Read-only dialog that shows articles split into two lists for a given prompt:
 * those with at least one update attempt (updated) and those without (not updated).
 */
class DialogViewUpdatedArticles : public QDialog
{
    Q_OBJECT
public:
    explicit DialogViewUpdatedArticles(const QList<PageRecord> &updated,
                                        const QList<PageRecord> &notUpdated,
                                        QWidget *parent = nullptr);
    ~DialogViewUpdatedArticles();

private:
    Ui::DialogViewUpdatedArticles *ui;
};

#endif // DIALOGVIEWUPDATEDARTICLES_H
