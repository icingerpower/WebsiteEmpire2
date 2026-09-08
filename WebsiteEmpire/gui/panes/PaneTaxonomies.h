#ifndef PANETAXONOMIES_H
#define PANETAXONOMIES_H
#include "website/taxonomy/TaxonomyDescriptor.h"
#include <QDir>
#include <QWidget>
#include <vector>

class AbstractEngine;
class AbstractPageBloc;
class QLabel;
class QScrollArea;

/**
 * Pane for managing taxonomy vocabularies.
 *
 * On show, discovers all taxonomy-aware blocs by iterating the ACTIVE
 * ENGINE's page types (AbstractEngine::getPageTypes() — the page types that
 * actually compose sites built with this engine, e.g. EngineArticles has
 * PageTypeArticleHealth + PageTypeJsApp; EngineArticlesFashion has only
 * PageTypeArticleFashion) and calling AbstractPageBloc::taxonomies() on each
 * bloc — a bloc may declare more than one taxonomy (e.g.
 * PageBlocFashionTaxonomyLinks declares ten: Color, Season, Occasion,
 * Material, Style Aesthetic, Product Type, Demographic, Fit/Silhouette,
 * Pattern, Culture), each getting its own independent card below.
 * This is deliberately NOT the global AbstractPageType registry
 * (AbstractPageType::allTypeIds()) — a Fashion site must never show the
 * Health-only "Symptoms" card just because PageTypeArticleHealth happens to
 * exist somewhere in the binary.
 *
 * For each unique taxonomy (by id), shows a card with:
 *   - Display name
 *   - Source DB path (saved in taxonomy_settings.ini) with a Browse button
 *   - Item count + last sync date
 *   - Sync button: reads items from the source aspire DB, writes to local taxonomy.db
 *
 * The taxonomy/ directory and taxonomy.db are created in the working directory
 * on first sync. Source paths are persisted across sessions.
 *
 * Adding a new taxonomy-aware bloc to an engine's page types automatically
 * adds a card here — no UI code changes.
 */
class PaneTaxonomies : public QWidget
{
    Q_OBJECT

public:
    explicit PaneTaxonomies(QWidget *parent = nullptr);
    ~PaneTaxonomies() override;

    // engine must outlive this pane's use (MainWindow owns both for the
    // lifetime of the open working directory) — non-owning, may be nullptr
    // before a working directory has been opened (setVisible() then shows
    // zero cards rather than crashing).
    void setup(const QDir &workingDir, const AbstractEngine *engine);

    void setVisible(bool visible) override;

private:
    struct TaxonomyEntry {
        TaxonomyDescriptor     descriptor;
        const AbstractPageBloc *bloc;       // non-owning — owned by m_engine's page type
        QLabel *sourceLabel = nullptr;  // non-owning, owned by scroll widget
        QLabel *statusLabel = nullptr;  // non-owning, owned by scroll widget
    };

    void _discover();
    void _buildCards();
    void _refreshStatus();
    void _onBrowse(int entryIndex);
    void _onSync(int entryIndex);

    QDir                  m_workingDir;
    bool                  m_isSetup  = false;
    bool                  m_built    = false;
    const AbstractEngine *m_engine   = nullptr;  // non-owning

    std::vector<TaxonomyEntry> m_entries;

    QScrollArea *m_scrollArea = nullptr;
    QWidget     *m_content    = nullptr;
};
#endif
