#ifndef PANETAXONOMIES_H
#define PANETAXONOMIES_H
#include "website/taxonomy/TaxonomyDescriptor.h"
#include <QDir>
#include <QWidget>
#include <memory>
#include <vector>

class AbstractPageBloc;
class AbstractPageType;
class CategoryTable;
class QLabel;
class QScrollArea;

/**
 * Pane for managing taxonomy vocabularies.
 *
 * On show, discovers all taxonomy-aware blocs by iterating the page type
 * registry and calling AbstractPageBloc::taxonomy() on each bloc.
 * For each unique taxonomy (by id), shows a card with:
 *   - Display name
 *   - Source DB path (saved in taxonomy_settings.ini) with a Browse button
 *   - Item count + last sync date
 *   - Sync button: reads items from the source aspire DB, writes to local taxonomy.db
 *
 * The taxonomy/ directory and taxonomy.db are created in the working directory
 * on first sync. Source paths are persisted across sessions.
 *
 * Adding a new taxonomy-aware bloc automatically adds a card here — no UI code changes.
 */
class PaneTaxonomies : public QWidget
{
    Q_OBJECT

public:
    explicit PaneTaxonomies(QWidget *parent = nullptr);
    ~PaneTaxonomies() override;

    void setup(const QDir &workingDir);

    void setVisible(bool visible) override;

private:
    struct TaxonomyEntry {
        TaxonomyDescriptor               descriptor;
        std::unique_ptr<AbstractPageType> pageType;  // keeps bloc alive
        const AbstractPageBloc           *bloc;       // non-owning
        QLabel *sourceLabel = nullptr;  // non-owning, owned by scroll widget
        QLabel *statusLabel = nullptr;  // non-owning, owned by scroll widget
    };

    void _discover();
    void _buildCards();
    void _refreshStatus();
    void _onBrowse(int entryIndex);
    void _onSync(int entryIndex);

    QDir                           m_workingDir;
    bool                           m_isSetup  = false;
    bool                           m_built    = false;
    std::unique_ptr<CategoryTable> m_categoryTable;

    std::vector<TaxonomyEntry> m_entries;

    QScrollArea *m_scrollArea = nullptr;
    QWidget     *m_content    = nullptr;
};
#endif
