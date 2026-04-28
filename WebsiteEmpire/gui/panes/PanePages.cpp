#include "PanePages.h"
#include "ui_PanePages.h"

#include "../dialogs/DialogPreviewPage.h"
#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/AbstractLegalPageDef.h"
#include "website/pages/PageTypeHome.h"
#include "website/pages/PageTypeLegal.h"
#include "website/pages/PageRecord.h"
#include "website/WebsiteSettingsTable.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/PageTranslator.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/widgets/DialogGenerateLegalPages.h"
#include "website/pages/widgets/PageEditorDialog.h"
#include "ExceptionWithTitleText.h"

#include "../../launcher/AbstractLauncher.h"
#include "../../launcher/LauncherTranslate.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMessageBox>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QSqlDatabase>
#include <QSqlQueryModel>

// ---------------------------------------------------------------------------
// EditableProxyModel — makes every cell show a line-edit on double-click so
// the user can select and copy text.  QSqlQueryModel::setData() returns false,
// so no data is ever written back.
// ---------------------------------------------------------------------------
class EditableProxyModel : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        return QSortFilterProxyModel::flags(index) | Qt::ItemIsEditable;
    }
};

// =============================================================================
// Constructor / Destructor
// =============================================================================

PanePages::PanePages(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PanePages)
{
    ui->setupUi(this);
    m_model      = new QSqlQueryModel(this);
    m_proxyModel = new EditableProxyModel(this);
    m_proxyModel->setSourceModel(m_model);
    ui->tableViewPages->setModel(m_proxyModel);
    ui->tableViewPages->setSortingEnabled(true);
    ui->tableViewPages->sortByColumn(0, Qt::AscendingOrder);
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
    const QList<int> ids = _selectedPageIds();
    if (ids.isEmpty()) {
        QMessageBox::warning(this, tr("No selection"), tr("Please select one or more pages to remove."));
        return;
    }
    const auto btn = QMessageBox::question(
        this,
        tr("Remove page(s)"),
        tr("Are you sure you want to remove %n page(s)? This action cannot be undone.", nullptr, ids.size()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (btn != QMessageBox::Yes) {
        return;
    }
    for (const int id : ids) {
        m_pageRepo->remove(id);
    }
    _refreshModel();
    _updateHomeButton();
    _updateLegalButton();
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
    DialogPreviewPage dlg(*m_pageRepo, *m_categoryTable, *m_engine, m_workingDir, id, this);
    dlg.exec();
}

void PanePages::generateHomePage()
{
    if (!m_pageRepo || !m_settingsTable) {
        return;
    }

    if (_homePageExists()) {
        QMessageBox::information(
            this,
            tr("Home page already exists"),
            tr("A home page already exists at /index.html.\n"
               "Select it in the list and use \"Edit page\" to modify it."));
        return;
    }

    const QString &editingLang = m_settingsTable->editingLangCode();
    m_pageRepo->create(
        QLatin1String(PageTypeHome::TYPE_ID),
        QStringLiteral("/index.html"),
        editingLang);

    _refreshModel();
    _updateHomeButton();

    QMessageBox::information(
        this,
        tr("Home page created"),
        tr("The home page has been created with permalink /index.html.\n"
           "Drogon serves it at http://<host>/index.html."));
}

void PanePages::generateLegalPages()
{
    if (!m_pageRepo || !m_settingsTable) {
        return;
    }

    try {
        _validateLegalSettings();
    } catch (const ExceptionWithTitleText &ex) {
        QMessageBox::warning(this, ex.errorTitle(), ex.errorText());
        return;
    }

    const QSet<QString> existingIds = _existingLegalDefIds();
    DialogGenerateLegalPages dlg(existingIds, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    const QList<QString> selected = dlg.selectedDefIds();
    if (selected.isEmpty()) {
        return;
    }

    const QString &editingLang = m_settingsTable->editingLangCode();

    // Build the LegalInfo once — shared across all pages in this generation run.
    const AbstractLegalPageDef::LegalInfo legalInfo {
        m_settingsTable->legalCompanyName(),
        m_settingsTable->legalCompanyAddress(),
        m_settingsTable->legalRegistrationNo(),
        m_settingsTable->legalContactEmail(),
        m_settingsTable->legalVatNo(),
        m_settingsTable->legalDpoName(),
        m_settingsTable->legalDpoEmail(),
        m_settingsTable->websiteName(),
    };

    for (const QString &defId : std::as_const(selected)) {
        const AbstractLegalPageDef *def = nullptr;
        for (const AbstractLegalPageDef *d : AbstractLegalPageDef::allDefs()) {
            if (d->getId() == defId) {
                def = d;
                break;
            }
        }
        if (!def) {
            continue;
        }

        if (existingIds.contains(defId)) {
            // Existing page explicitly selected: restore default permalink and
            // regenerate content from current settings.
            const int pid = _legalPageId(defId);
            if (pid > 0) {
                m_pageRepo->updatePermalink(pid, def->getDefaultPermalink());
                QHash<QString, QString> data;
                data.insert(QLatin1String(AbstractLegalPageDef::PAGE_DATA_KEY_DEF_ID), defId);
                data.insert(QStringLiteral("1_text"), def->generateTextContent(legalInfo));
                m_pageRepo->saveData(pid, data);
            }
        } else {
            // New page: create, stamp with the def ID, and populate with generated content.
            const int newId = m_pageRepo->create(
                QLatin1String(PageTypeLegal::TYPE_ID),
                def->getDefaultPermalink(),
                editingLang);
            QHash<QString, QString> data;
            data.insert(QLatin1String(AbstractLegalPageDef::PAGE_DATA_KEY_DEF_ID), defId);
            // "1_text" = bloc index 1 (PageBlocText), key PageBlocText::KEY_TEXT = "text".
            data.insert(QStringLiteral("1_text"), def->generateTextContent(legalInfo));
            m_pageRepo->saveData(newId, data);
        }
    }

    _refreshModel();
    _updateLegalButton();
}

void PanePages::translate()
{
    if (!m_pageRepo || !m_categoryTable) {
        QMessageBox::warning(this, tr("Not ready"),
                             tr("Pages database is not open. Switch to this tab first."));
        return;
    }
    if (!m_engine) {
        QMessageBox::warning(this, tr("No engine"),
                             tr("No website engine is configured. "
                                "Please select an engine before translating."));
        return;
    }

    const QString editingLang = m_settingsTable
                                    ? m_settingsTable->editingLangCode()
                                    : QString();
    if (editingLang.isEmpty()) {
        QMessageBox::warning(this, tr("No editing language"),
                             tr("The editing language is not configured in Settings."));
        return;
    }

    qDebug() << "[Translate] Starting. Editing lang:" << editingLang;

    auto *translator = new PageTranslator(*m_pageRepo, *m_categoryTable,
                                          m_workingDir, this);

    connect(translator, &PageTranslator::logMessage, this, [](const QString &msg) {
        qDebug() << "[Translate]" << qPrintable(msg);
    });

    connect(translator, &PageTranslator::pageTranslated, this, [this](int pageId) {
        qDebug() << "[Translate] Page translated, id=" << pageId;
        _refreshModel();
    });

    connect(translator, &PageTranslator::finished, this,
            [this, translator](int translated, int errors) {
                qDebug() << "[Translate] Done. Translated:" << translated
                         << " Errors:" << errors;
                _refreshModel();
                if (errors > 0) {
                    QMessageBox::warning(
                        this,
                        tr("Translation completed with errors"),
                        tr("Translated: %1 page(s)\nErrors: %2\n\n"
                           "Check translation_logs/ in the working directory for details.")
                            .arg(translated)
                            .arg(errors));
                } else {
                    QMessageBox::information(
                        this,
                        tr("Translation complete"),
                        tr("Successfully translated %1 page(s).").arg(translated));
                }
                translator->deleteLater();
            });

    translator->start(m_engine, editingLang);
}

void PanePages::copyUrl()
{
    const int id = _selectedPageId();
    if (id < 0) {
        QMessageBox::warning(this, tr("No selection"), tr("Please select exactly one page."));
        return;
    }
    if (!m_engine || m_engine->rowCount() == 0) {
        QMessageBox::warning(this, tr("No engine"), tr("No engine is configured."));
        return;
    }

    // Find the first row with lang code "en".
    QString domain;
    const int rows = m_engine->rowCount();
    for (int i = 0; i < rows; ++i) {
        const QString &lang = m_engine->data(
            m_engine->index(i, AbstractEngine::COL_LANG_CODE)).toString();
        if (lang == QLatin1String("en")) {
            domain = m_engine->data(
                m_engine->index(i, AbstractEngine::COL_DOMAIN)).toString().trimmed();
            break;
        }
    }

    if (domain.isEmpty()) {
        QMessageBox::warning(this, tr("No English domain"),
                             tr("No engine row with language code \"en\" found."));
        return;
    }

    const QModelIndex idx     = ui->tableViewPages->currentIndex();
    const QModelIndex srcIdx  = m_proxyModel->mapToSource(idx);
    const QString &permalink  = m_model->data(m_model->index(srcIdx.row(), 2)).toString();

    const QString url = QStringLiteral("https://") + domain + permalink;
    QGuiApplication::clipboard()->setText(url);
}

void PanePages::viewCommandTranslate()
{
    const QString cmd = QStringLiteral("WebsiteEmpire --%1 \"%2\" --%3")
        .arg(AbstractLauncher::OPTION_WORKING_DIR,
             m_workingDir.absolutePath(),
             LauncherTranslate::OPTION_NAME);
    QMessageBox::information(this,
        tr("Command Line — Translate"),
        tr("Run the following command to translate all pending pages without launching the UI:\n\n%1")
            .arg(cmd));
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
    connect(ui->buttonGenerateHomePage,      &QPushButton::clicked, this, &PanePages::generateHomePage);
    connect(ui->buttonGenerateLegalPages,    &QPushButton::clicked, this, &PanePages::generateLegalPages);
    connect(ui->buttonTranslate,    &QPushButton::clicked, this, &PanePages::translate);
    connect(ui->buttonViewCommandTranslate,    &QPushButton::clicked, this, &PanePages::viewCommandTranslate);
    connect(ui->buttonFilter,      &QPushButton::clicked, this, &PanePages::_applyTypeFilter);
    connect(ui->buttonCopyUrl,     &QPushButton::clicked, this, &PanePages::copyUrl);
    connect(ui->buttonResetFilter, &QPushButton::clicked, this, [this]() {
        ui->comboBoxPageType->setCurrentIndex(0);
        _applyTypeFilter();
    });
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

    // Populate type filter combo (first entry = show all).
    ui->comboBoxPageType->clear();
    ui->comboBoxPageType->addItem(tr("All page types"), QString());
    for (const QString &typeId : AbstractPageType::allTypeIds()) {
        const auto pageType = AbstractPageType::createForTypeId(typeId, *m_categoryTable);
        if (pageType) {
            ui->comboBoxPageType->addItem(pageType->getDisplayName(), typeId);
        }
    }

    _refreshModel();
    _updateHomeButton();
    _updateLegalButton();
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
    const QModelIndex srcIdx = m_proxyModel->mapToSource(idx);
    const QModelIndex idIdx  = m_model->index(srcIdx.row(), 0);
    return m_model->data(idIdx).toInt();
}

QList<int> PanePages::_selectedPageIds() const
{
    const QModelIndexList selected =
        ui->tableViewPages->selectionModel()->selectedRows();
    QList<int> ids;
    ids.reserve(selected.size());
    for (const QModelIndex &proxyIdx : selected) {
        const QModelIndex srcIdx = m_proxyModel->mapToSource(proxyIdx);
        const QModelIndex idIdx  = m_model->index(srcIdx.row(), 0);
        ids.append(m_model->data(idIdx).toInt());
    }
    return ids;
}

void PanePages::_applyTypeFilter()
{
    const QString typeId = ui->comboBoxPageType->currentData().toString();
    const int rowCount   = m_proxyModel->rowCount();
    for (int row = 0; row < rowCount; ++row) {
        bool hide = false;
        if (!typeId.isEmpty()) {
            const QModelIndex typeIdx = m_proxyModel->index(row, 1);
            hide = (m_proxyModel->data(typeIdx).toString() != typeId);
        }
        ui->tableViewPages->setRowHidden(row, hide);
    }
}

QSet<QString> PanePages::_existingLegalDefIds() const
{
    QSet<QString> result;
    if (!m_pageRepo || !m_settingsTable) {
        return result;
    }

    const QString &editingLang = m_settingsTable->editingLangCode();
    const QList<PageRecord> &pages = m_pageRepo->findSourcePages(editingLang);

    for (const PageRecord &p : std::as_const(pages)) {
        if (p.typeId != QLatin1String(PageTypeLegal::TYPE_ID)) {
            continue;
        }
        const QHash<QString, QString> &data = m_pageRepo->loadData(p.id);
        const QString &defId = data.value(
            QLatin1String(AbstractLegalPageDef::PAGE_DATA_KEY_DEF_ID));
        if (!defId.isEmpty()) {
            result.insert(defId);
        }
    }

    return result;
}

int PanePages::_legalPageId(const QString &defId) const
{
    if (!m_pageRepo || !m_settingsTable) {
        return -1;
    }

    const QString &editingLang = m_settingsTable->editingLangCode();
    const QList<PageRecord> &pages = m_pageRepo->findSourcePages(editingLang);

    for (const PageRecord &p : std::as_const(pages)) {
        if (p.typeId != QLatin1String(PageTypeLegal::TYPE_ID)) {
            continue;
        }
        const QHash<QString, QString> &data = m_pageRepo->loadData(p.id);
        if (data.value(QLatin1String(AbstractLegalPageDef::PAGE_DATA_KEY_DEF_ID)) == defId) {
            return p.id;
        }
    }

    return -1;
}

void PanePages::_validateLegalSettings() const
{
    struct Field {
        const QString &id;
        const char    *label;
    };
    const QList<Field> mandatory = {
        { WebsiteSettingsTable::ID_LEGAL_COMPANY_NAME,    "Company name" },
        { WebsiteSettingsTable::ID_LEGAL_COMPANY_ADDRESS, "Company address" },
        { WebsiteSettingsTable::ID_LEGAL_REGISTRATION_NO, "Registration number" },
        { WebsiteSettingsTable::ID_LEGAL_CONTACT_EMAIL,   "Legal contact email" },
    };

    QStringList missing;
    for (const auto &f : std::as_const(mandatory)) {
        if (m_settingsTable->valueForId(f.id).trimmed().isEmpty()) {
            missing.append(tr(f.label));
        }
    }

    if (!missing.isEmpty()) {
        ExceptionWithTitleText ex(
            tr("Missing legal information"),
            tr("The following fields must be filled in Settings before generating legal pages:\n\n%1")
                .arg(missing.join(QStringLiteral("\n"))));
        ex.raise();
    }
}

bool PanePages::_homePageExists() const
{
    if (!m_pageRepo || !m_settingsTable) {
        return false;
    }
    const QString &editingLang = m_settingsTable->editingLangCode();
    const QList<PageRecord> &pages = m_pageRepo->findSourcePages(editingLang);
    for (const PageRecord &p : std::as_const(pages)) {
        if (p.typeId == QLatin1String(PageTypeHome::TYPE_ID)) {
            return true;
        }
    }
    return false;
}

void PanePages::_updateHomeButton()
{
    if (!m_pageRepo || !m_settingsTable) {
        return;
    }
    ui->buttonGenerateHomePage->setStyleSheet(
        _homePageExists() ? QStringLiteral("color: grey;") : QString());
}

void PanePages::_updateLegalButton()
{
    if (!m_pageRepo || !m_settingsTable) {
        return;
    }

    const QSet<QString> &existingIds = _existingLegalDefIds();
    bool allGenerated = true;
    for (const AbstractLegalPageDef *def : AbstractLegalPageDef::allDefs()) {
        if (!existingIds.contains(def->getId())) {
            allGenerated = false;
            break;
        }
    }

    // Grey text signals "all done"; default (empty) style signals pages are missing.
    ui->buttonGenerateLegalPages->setStyleSheet(
        allGenerated ? QStringLiteral("color: grey;") : QString());
}
