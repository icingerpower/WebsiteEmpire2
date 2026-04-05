#include "PagesListWidget.h"
#include "ui_PagesListWidget.h"

#include "website/AbstractEngine.h"
#include "website/WebsiteSettingsTable.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageGenerator.h"
#include "website/pages/widgets/PageEditorDialog.h"

#include <QMessageBox>
#include <QSqlQueryModel>

PagesListWidget::PagesListWidget(IPageRepository      &repo,
                                 CategoryTable        &categoryTable,
                                 const QDir           &workingDir,
                                 const QString        &domain,
                                 AbstractEngine       &engine,
                                 int                   websiteIndex,
                                 WebsiteSettingsTable *settingsTable,
                                 QWidget              *parent)
    : QWidget(parent)
    , ui(new Ui::PagesListWidget)
    , m_repo(repo)
    , m_categoryTable(categoryTable)
    , m_workingDir(workingDir)
    , m_domain(domain)
    , m_engine(engine)
    , m_websiteIndex(websiteIndex)
    , m_settingsTable(settingsTable)
    , m_model(new QSqlQueryModel(this))
{
    ui->setupUi(this);
    ui->tableView->setModel(m_model);

    connect(ui->btnNew,      &QPushButton::clicked,
            this, &PagesListWidget::_onNewClicked);
    connect(ui->btnEdit,     &QPushButton::clicked,
            this, &PagesListWidget::_onEditClicked);
    connect(ui->btnDelete,   &QPushButton::clicked,
            this, &PagesListWidget::_onDeleteClicked);
    connect(ui->btnGenerate, &QPushButton::clicked,
            this, &PagesListWidget::_onGenerateClicked);
    connect(ui->tableView->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this, &PagesListWidget::_onSelectionChanged);

    _refreshModel();
}

PagesListWidget::~PagesListWidget()
{
    delete ui;
}

// =============================================================================
// Private helpers
// =============================================================================

void PagesListWidget::_refreshModel()
{
    // Re-query from the live pages table.  The model is SQL-backed so the
    // QTableView reflects the latest data without manual row insertion.
    // Column order must match the header labels set below.
    m_model->setQuery(
        QStringLiteral("SELECT id, type_id, permalink, lang, updated_at"
                       " FROM pages ORDER BY id ASC"));
    m_model->setHeaderData(0, Qt::Horizontal, tr("ID"));
    m_model->setHeaderData(1, Qt::Horizontal, tr("Type"));
    m_model->setHeaderData(2, Qt::Horizontal, tr("Permalink"));
    m_model->setHeaderData(3, Qt::Horizontal, tr("Lang"));
    m_model->setHeaderData(4, Qt::Horizontal, tr("Updated"));

    ui->tableView->resizeColumnsToContents();
    _onSelectionChanged();
}

int PagesListWidget::_selectedPageId() const
{
    const QModelIndex idx = ui->tableView->currentIndex();
    if (!idx.isValid()) {
        return -1;
    }
    return m_model->data(m_model->index(idx.row(), 0)).toInt();
}

// =============================================================================
// Slots
// =============================================================================

void PagesListWidget::_onSelectionChanged()
{
    const bool hasSelection = ui->tableView->currentIndex().isValid();
    ui->btnEdit->setEnabled(hasSelection);
    ui->btnDelete->setEnabled(hasSelection);
}

void PagesListWidget::_onNewClicked()
{
    const QString langCode = m_settingsTable ? m_settingsTable->editingLangCode() : QString();
    PageEditorDialog dlg(m_repo, m_categoryTable, -1, langCode, this);
    if (dlg.exec() == QDialog::Accepted) {
        _refreshModel();
    }
}

void PagesListWidget::_onEditClicked()
{
    const int id = _selectedPageId();
    if (id < 0) {
        return;
    }
    PageEditorDialog dlg(m_repo, m_categoryTable, id, {}, this);
    if (dlg.exec() == QDialog::Accepted) {
        _refreshModel();
    }
}

void PagesListWidget::_onDeleteClicked()
{
    const int id = _selectedPageId();
    if (id < 0) {
        return;
    }
    const int answer = QMessageBox::question(
        this,
        tr("Delete page"),
        tr("Delete page %1? This cannot be undone.").arg(id),
        QMessageBox::Yes | QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        m_repo.remove(id);
        _refreshModel();
    }
}

void PagesListWidget::_onGenerateClicked()
{
    PageGenerator gen(m_repo, m_categoryTable);
    const int count = gen.generateAll(m_workingDir, m_domain, m_engine, m_websiteIndex);
    QMessageBox::information(this,
                             tr("Generation complete"),
                             tr("Generated %1 page(s).").arg(count));
    emit generated(count);
}
