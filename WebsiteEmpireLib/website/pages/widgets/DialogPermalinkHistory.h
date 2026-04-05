#ifndef DIALOGPERMALINKHISTORY_H
#define DIALOGPERMALINKHISTORY_H

#include <QDialog>

class IPageRepository;

namespace Ui { class DialogPermalinkHistory; }

/**
 * Shows the permalink history for a single page and lets the user change
 * the redirect type for each old entry.
 *
 * Columns: Old Permalink (read-only) | Changed At (read-only) | Redirect Type (combo)
 *
 * Redirect type options:
 *   Permanent 301  — default; the old URL 301-redirects to the current permalink
 *   Parked    302  — temporary redirect (content may return); 302
 *   Deleted   410  — content was intentionally removed; 410 Gone, no forwarding
 *
 * Changes are saved immediately on combo selection change.
 *
 * The dialog holds a non-owning reference to IPageRepository; it must outlive
 * this dialog.
 */
class DialogPermalinkHistory : public QDialog
{
    Q_OBJECT

public:
    explicit DialogPermalinkHistory(IPageRepository &repo,
                                    int              pageId,
                                    QWidget         *parent = nullptr);
    ~DialogPermalinkHistory() override;

private:
    void _populate();

    Ui::DialogPermalinkHistory *ui;
    IPageRepository            &m_repo;
    int                         m_pageId;
};

#endif // DIALOGPERMALINKHISTORY_H
