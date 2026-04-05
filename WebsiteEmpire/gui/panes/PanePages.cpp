#include "PanePages.h"
#include "ui_PanePages.h"

#include "../dialogs/DialogPreviewPage.h"
#include "website/AbstractEngine.h"
#include "website/WebsiteSettingsTable.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/widgets/PageEditorDialog.h"

#include <QMessageBox>
#include <QSqlDatabase>
#include <QSqlQueryModel>

// =============================================================================
// Constructor / Destructor
// =============================================================================

PanePages::PanePages(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PanePages)
{
    ui->setupUi(this);
    m_model = new QSqlQueryModel(this);
    ui->tableViewPages->setModel(m_model);
    _connectSlots();
}

PanePages::~PanePages()
{
    // Destroy repo before db to avoid dangling reference.
    m_pageRepo.reset();
    m_pageDb.reset();
    delete ui;
}

// =============================================================================
// Public
// =============================================================================

void PanePages::setup(const QDir &workingDir, AbstractEngine *engine, WebsiteSettingsTable *settingsTable)
{
    m_workingDir    = workingDir;
    m_engine        = engine;
    m_settingsTable = settingsTable;
    m_isSetup       = true;
}

// =============================================================================
// Public slots
// =============================================================================

void PanePages::addPage()
{
    if (!m_pageRepo || !m_categoryTable) {
        return;
    }
    const QString langCode = m_settingsTable ? m_settingsTable->editingLangCode() : QString();
    PageEditorDialog dlg(*m_pageRepo, *m_categoryTable, -1, langCode, this);
    if (dlg.exec() == QDialog::Accepted) {
        _refreshModel();
    }
}

void PanePages::editPage()
{
    const int id = _selectedPageId();
    if (id < 0) {
        QMessageBox::warning(this, tr("No selection"), tr("Please select a page to edit."));
        return;
    }
    PageEditorDialog dlg(*m_pageRepo, *m_categoryTable, id, {}, this);
    if (dlg.exec() == QDialog::Accepted) {
        _refreshModel();
    }
}

void PanePages::removePage()
{
    const int id = _selectedPageId();
    if (id < 0) {
        QMessageBox::warning(this, tr("No selection"), tr("Please select a page to remove."));
        return;
    }
    const auto btn = QMessageBox::question(
        this,
        tr("Remove page"),
        tr("Are you sure you want to remove this page? This action cannot be undone."),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (btn != QMessageBox::Yes) {
        return;
    }
    m_pageRepo->remove(id);
    _refreshModel();
}

void PanePages::previewPage()
{
    const int id = _selectedPageId();
    if (id < 0) {
        QMessageBox::warning(this, tr("No selection"), tr("Please select a page to preview."));
        return;
    }
    if (!m_engine) {
        QMessageBox::warning(this,
                             tr("No engine"),
                             tr("No website engine is configured. "
                                "Please select an engine before previewing."));
        return;
    }
    DialogPreviewPage dlg(*m_pageRepo, *m_categoryTable, *m_engine, id, this);
    dlg.exec();
}

void PanePages::setVisible(bool visible)
{
    if (visible && m_isSetup) {
        const bool needInit = !m_pageDb || !m_pageDb->database().isOpen();
        if (needInit) {
            _initDb();
        } else {
            _refreshModel();
        }
    }
    QWidget::setVisible(visible);
}

// =============================================================================
// Private helpers
// =============================================================================

void PanePages::_connectSlots()
{
    connect(ui->buttonAddPage,    &QPushButton::clicked, this, &PanePages::addPage);
    connect(ui->buttonEditPage,   &QPushButton::clicked, this, &PanePages::editPage);
    connect(ui->buttonRemovePage, &QPushButton::clicked, this, &PanePages::removePage);
    connect(ui->buttonPreview,    &QPushButton::clicked, this, &PanePages::previewPage);
}

void PanePages::_initDb()
{
    // Tear down in reverse dependency order: repo holds a ref to db.
    m_pageRepo.reset();
    m_pageDb.reset();
    m_categoryTable.reset();

    m_categoryTable = std::make_unique<CategoryTable>(m_workingDir);
    m_pageDb        = std::make_unique<PageDb>(m_workingDir);
    m_pageRepo      = std::make_unique<PageRepositoryDb>(*m_pageDb);

    _refreshModel();
}

void PanePages::_refreshModel()
{
    if (!m_pageDb) {
        return;
    }
    m_model->setQuery(
        QStringLiteral(
            "SELECT id, type_id, permalink, lang, updated_at FROM pages ORDER BY id"),
        m_pageDb->database());
    m_model->setHeaderData(0, Qt::Horizontal, tr("ID"));
    m_model->setHeaderData(1, Qt::Horizontal, tr("Type"));
    m_model->setHeaderData(2, Qt::Horizontal, tr("Permalink"));
    m_model->setHeaderData(3, Qt::Horizontal, tr("Lang"));
    m_model->setHeaderData(4, Qt::Horizontal, tr("Updated"));
    if (m_model->rowCount() > 0 && !m_resizedOnce) {
        ui->tableViewPages->resizeColumnsToContents();
        m_resizedOnce = true;
    }
}

int PanePages::_selectedPageId() const
{
    const QModelIndex idx = ui->tableViewPages->currentIndex();
    if (!idx.isValid()) {
        return -1;
    }
    const QModelIndex idIdx = m_model->index(idx.row(), 0);
    return m_model->data(idIdx).toInt();
}
