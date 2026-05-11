#include "PaneGeneratedPages.h"
#include "ui_PaneGeneratedPages.h"

#include "website/AbstractEngine.h"
#include "website/WebsiteSettingsTable.h"
#include "website/pages/CategoryHubDirtySet.h"
#include "website/pages/CategoryHubSyncer.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageGenerator.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/PageTypeCategory.h"
#include "website/pages/attributes/CategoryTable.h"
#include "../dialogs/DialogPreviewPage.h"
#include "ExceptionWithTitleText.h"

#include <QAbstractTableModel>
#include <QMessageBox>

// ---------------------------------------------------------------------------
// GeneratedPagesModel — flat, read-only table backing the QTableView.
// Defined here (not in a separate header) because it is only used by this pane.
// No Q_OBJECT: the pane does not need custom signals from the model, and
// omitting the macro avoids a separate .moc compilation unit.
// ---------------------------------------------------------------------------

namespace {

struct GeneratedPageRow {
    int     id          = 0;
    QString typeDisplay;
    QString name;
    QString permalink;
    QString status;
    QString generatedAt;
};

} // namespace

class GeneratedPagesModel : public QAbstractTableModel
{
public:
    enum Column {
        COL_TYPE         = 0,
        COL_NAME         = 1,
        COL_PERMALINK    = 2,
        COL_STATUS       = 3,
        COL_GENERATED_AT = 4,
        COL_COUNT        = 5
    };

    explicit GeneratedPagesModel(QObject *parent = nullptr);

    void setRows(QList<GeneratedPageRow> rows);

    int idForRow(int row) const;

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int      columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    QList<GeneratedPageRow> m_rows;
};

GeneratedPagesModel::GeneratedPagesModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void GeneratedPagesModel::setRows(QList<GeneratedPageRow> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}

int GeneratedPagesModel::idForRow(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return -1;
    }
    return m_rows.at(row).id;
}

int GeneratedPagesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int GeneratedPagesModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : COL_COUNT;
}

QVariant GeneratedPagesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    const GeneratedPageRow &row = m_rows.at(index.row());
    switch (index.column()) {
    case COL_TYPE:         return row.typeDisplay;
    case COL_NAME:         return row.name;
    case COL_PERMALINK:    return row.permalink;
    case COL_STATUS:       return row.status;
    case COL_GENERATED_AT: return row.generatedAt;
    default:               return {};
    }
}

QVariant GeneratedPagesModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case COL_TYPE:         return QStringLiteral("Type");
    case COL_NAME:         return QStringLiteral("Name");
    case COL_PERMALINK:    return QStringLiteral("Permalink");
    case COL_STATUS:       return QStringLiteral("Status");
    case COL_GENERATED_AT: return QStringLiteral("Generated");
    default:               return {};
    }
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

PaneGeneratedPages::PaneGeneratedPages(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneGeneratedPages)
{
    ui->setupUi(this);
    m_pagesModel = new GeneratedPagesModel(this);
    ui->tableViewPages->setModel(m_pagesModel);
    _connectSlots();
}

PaneGeneratedPages::~PaneGeneratedPages()
{
    // Tear down in reverse dependency order.
    m_syncer.reset();
    m_dirtySet.reset();
    m_pageGenerator.reset();
    m_pageRepo.reset();
    m_pageDb.reset();
    m_categoryTable.reset();
    delete ui;
}

// =============================================================================
// Public
// =============================================================================

void PaneGeneratedPages::setup(const QDir &workingDir,
                               AbstractEngine *engine,
                               WebsiteSettingsTable *settingsTable)
{
    m_workingDir    = workingDir;
    m_engine        = engine;
    m_settingsTable = settingsTable;
    m_isSetup       = true;
}

void PaneGeneratedPages::setVisible(bool visible)
{
    if (visible && m_isSetup) {
        const bool needInit = !m_pageDb || !m_pageDb->database().isOpen();
        if (needInit) {
            _initDb();
        }
        // Sync stubs on every show so the list is always up to date.
        syncStubs();
    }
    QWidget::setVisible(visible);
}

// =============================================================================
// Public slots
// =============================================================================

void PaneGeneratedPages::syncStubs()
{
    if (!m_syncer) {
        return;
    }
    if (m_settingsTable) {
        m_syncer->syncStubs(m_settingsTable->editingLangCode());
    }
    _refreshModel();
}

void PaneGeneratedPages::reRenderStale()
{
    if (!m_syncer || !m_dirtySet) {
        return;
    }
    if (m_dirtySet->isEmpty()) {
        QMessageBox::information(this, tr("Re-render stale"),
                                 tr("No pages are currently marked as stale."));
        return;
    }
    if (!m_engine || m_engine->rowCount() == 0) {
        QMessageBox::warning(this, tr("Re-render stale"),
                             tr("No engine configured. Set up at least one domain row first."));
        return;
    }
    try {
        const int count = m_syncer->renderDirtyHubs(
            m_workingDir, _firstDomain(), *m_engine, 0);
        _refreshModel();
        QMessageBox::information(this, tr("Re-render stale"),
                                 tr("Re-rendered %n page(s).", nullptr, count));
    } catch (const ExceptionWithTitleText &ex) {
        QMessageBox::critical(this, ex.errorTitle(), ex.errorText());
    }
}

void PaneGeneratedPages::reRenderSelected()
{
    const QList<int> ids = _selectedPageIds();
    if (ids.isEmpty()) {
        QMessageBox::warning(this, tr("No selection"),
                             tr("Please select one or more pages to re-render."));
        return;
    }
    if (!m_syncer || !m_dirtySet) {
        return;
    }
    if (!m_engine || m_engine->rowCount() == 0) {
        QMessageBox::warning(this, tr("Re-render selected"),
                             tr("No engine configured. Set up at least one domain row first."));
        return;
    }
    try {
        for (const int id : std::as_const(ids)) {
            m_dirtySet->add(id);
        }
        const int count = m_syncer->renderDirtyHubs(
            m_workingDir, _firstDomain(), *m_engine, 0);
        _refreshModel();
        QMessageBox::information(this, tr("Re-render selected"),
                                 tr("Re-rendered %n page(s).", nullptr, count));
    } catch (const ExceptionWithTitleText &ex) {
        QMessageBox::critical(this, ex.errorTitle(), ex.errorText());
    }
}

// =============================================================================
// Private helpers
// =============================================================================

void PaneGeneratedPages::_connectSlots()
{
    connect(ui->buttonSyncStubs,        &QPushButton::clicked, this, &PaneGeneratedPages::syncStubs);
    connect(ui->buttonReRenderStale,    &QPushButton::clicked, this, &PaneGeneratedPages::reRenderStale);
    connect(ui->buttonReRenderSelected, &QPushButton::clicked, this, &PaneGeneratedPages::reRenderSelected);
    connect(ui->buttonPreview,          &QPushButton::clicked, this, &PaneGeneratedPages::previewPage);
}

void PaneGeneratedPages::_initDb()
{
    // Tear down in reverse dependency order.
    m_syncer.reset();
    m_dirtySet.reset();
    m_pageGenerator.reset();
    m_pageRepo.reset();
    m_pageDb.reset();
    m_categoryTable.reset();

    m_categoryTable = std::make_unique<CategoryTable>(m_workingDir);
    m_pageDb        = std::make_unique<PageDb>(m_workingDir);
    m_pageRepo      = std::make_unique<PageRepositoryDb>(*m_pageDb);
    m_pageGenerator = std::make_unique<PageGenerator>(*m_pageRepo, *m_categoryTable);
    m_dirtySet      = std::make_unique<CategoryHubDirtySet>(m_workingDir);
    m_syncer        = std::make_unique<CategoryHubSyncer>(
                          *m_pageRepo, *m_categoryTable, *m_dirtySet, *m_pageGenerator);
}

void PaneGeneratedPages::_refreshModel()
{
    if (!m_pageRepo || !m_categoryTable || !m_dirtySet) {
        return;
    }
    QList<GeneratedPageRow> rows;
    const QList<PageRecord> all = m_pageRepo->findAll();
    for (const PageRecord &record : std::as_const(all)) {
        if (record.typeId != QLatin1String(PageTypeCategory::TYPE_ID)) {
            continue;
        }
        if (record.sourcePageId != 0) {
            continue; // skip translation pages — show source pages only
        }

        const QHash<QString, QString> data = m_pageRepo->loadData(record.id);
        const QSet<int> catIds = CategoryHubSyncer::extractCategoryIds(data);

        QStringList catNames;
        for (const int catId : std::as_const(catIds)) {
            const CategoryTable::CategoryRow *cat = m_categoryTable->categoryById(catId);
            if (cat) {
                catNames.append(cat->name);
            }
        }
        catNames.sort();

        GeneratedPageRow row;
        row.id          = record.id;
        row.typeDisplay = tr("Category hub");
        row.name        = catNames.join(QStringLiteral(", "));
        row.permalink   = record.permalink;
        row.generatedAt = record.generatedAt;

        if (record.generatedAt.isEmpty()) {
            row.status = tr("Stub");
        } else if (m_dirtySet->contains(record.id)) {
            row.status = tr("Stale");
        } else {
            row.status = tr("Ready");
        }

        rows.append(row);
    }
    m_pagesModel->setRows(std::move(rows));
    ui->tableViewPages->resizeColumnsToContents();
}

void PaneGeneratedPages::previewPage()
{
    const int id = _selectedPageId();
    if (id < 0) {
        QMessageBox::warning(this, tr("No selection"),
                             tr("Please select a page to preview."));
        return;
    }
    if (!m_engine) {
        QMessageBox::warning(this, tr("No engine"),
                             tr("No website engine is configured. "
                                "Please select an engine before previewing."));
        return;
    }
    DialogPreviewPage dlg(*m_pageRepo, *m_categoryTable, *m_engine, m_workingDir, id, this);
    dlg.exec();
}

int PaneGeneratedPages::_selectedPageId() const
{
    const QModelIndex idx = ui->tableViewPages->currentIndex();
    if (!idx.isValid()) {
        return -1;
    }
    return m_pagesModel->idForRow(idx.row());
}

QList<int> PaneGeneratedPages::_selectedPageIds() const
{
    const QModelIndexList selected =
        ui->tableViewPages->selectionModel()->selectedRows();
    QList<int> ids;
    ids.reserve(selected.size());
    for (const QModelIndex &idx : std::as_const(selected)) {
        const int id = m_pagesModel->idForRow(idx.row());
        if (id > 0) {
            ids.append(id);
        }
    }
    return ids;
}

QString PaneGeneratedPages::_firstDomain() const
{
    if (!m_engine || m_engine->rowCount() == 0) {
        return QStringLiteral("localhost");
    }
    const QString raw = m_engine->data(
        m_engine->index(0, AbstractEngine::COL_DOMAIN)).toString().trimmed();
    return raw.isEmpty() ? QStringLiteral("localhost") : raw;
}
