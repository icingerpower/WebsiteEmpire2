#ifndef DIALOGGENERATEPHASE2_H
#define DIALOGGENERATEPHASE2_H

#include "website/pages/PageRecord.h"

#include <QDialog>
#include <QHash>
#include <QList>

namespace Ui { class DialogGeneratePhase2; }

/**
 * Lists all Complete pages (social-media second pass not yet run) and lets
 * the user choose which ones to queue for phase 2 generation.
 *
 * Items are displayed as "#ID  /permalink  [typeId]" and sorted by ID by default.
 * A sort combo lets the user switch to "By Permalink" or "By Strategy" (the latter
 * is only offered when pages span more than one typeId).
 *
 * A live filter (permalink text + page type) hides non-matching rows without
 * changing their check state.  selectedPageIds() only returns IDs of items
 * that are both checked AND currently visible (i.e. match the filter).
 *
 * All items start checked so the common case (run phase 2 on everything) needs
 * no extra interaction.
 */
class DialogGeneratePhase2 : public QDialog
{
    Q_OBJECT

public:
    explicit DialogGeneratePhase2(const QList<PageRecord> &pages,
                                   QWidget *parent = nullptr);
    ~DialogGeneratePhase2();

    // Page IDs of items that are checked AND visible after filtering.
    QList<int> selectedPageIds() const;

private slots:
    void _checkAll();
    void _uncheckAll();
    void _applyFilter();
    void _applySort();
    void _updateCount();

private:
    // Clears and repopulates listWidget according to the current sort combo,
    // restoring check states from m_checkStates (defaults to Checked for new items).
    void _rebuildList();

    Ui::DialogGeneratePhase2   *ui;
    QList<PageRecord>           m_pages;
    QHash<int, Qt::CheckState>  m_checkStates; // keyed by page id; persists across sorts
};

#endif // DIALOGGENERATEPHASE2_H
