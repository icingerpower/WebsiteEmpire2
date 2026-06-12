#include "PaneSettings.h"
#include "ui_PaneSettings.h"

#include "website/WebsiteSettingsTable.h"
#include "website/ImageWriter.h"
#include "website/theme/AbstractTheme.h"
#include "workingdirectory/WorkingDirectoryManager.h"

#include "aicli/AbstractCli.h"
#include "aicli/AvailableCliList.h"
#include "aicli/AvailableCliTable.h"

#include <QAbstractItemModel>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QLocale>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStyledItemDelegate>

namespace {

// Delegate that shows a QComboBox for rows whose model returns a non-empty
// QStringList via AllowedValuesRole, and falls back to the default line-edit
// for unconstrained rows.
class SettingsDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        const QStringList allowed = index.data(WebsiteSettingsTable::AllowedValuesRole).toStringList();
        if (allowed.isEmpty()) {
            return QStyledItemDelegate::createEditor(parent, option, index);
        }
        auto *combo = new QComboBox(parent);
        for (const QString &code : std::as_const(allowed)) {
            const QString native = QLocale(code).nativeLanguageName();
            const QString label  = native.isEmpty()
                                       ? code
                                       : QStringLiteral("%1 — %2").arg(code, native);
            combo->addItem(label, code);
        }
        return combo;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo) {
            QStyledItemDelegate::setEditorData(editor, index);
            return;
        }
        const QString current = index.data(Qt::EditRole).toString();
        const int i = combo->findData(current);
        if (i >= 0) {
            combo->setCurrentIndex(i);
        }
    }

    void setModelData(QWidget *editor,
                      QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo) {
            QStyledItemDelegate::setModelData(editor, model, index);
            return;
        }
        model->setData(index, combo->currentData(), Qt::EditRole);
    }

    void updateEditorGeometry(QWidget *editor,
                              const QStyleOptionViewItem &option,
                              const QModelIndex &) const override
    {
        editor->setGeometry(option.rect);
    }
};

// Reads the file at path, writes it to images.db with empty domain (global
// wildcard) and canonical filename, and returns true on success.
bool writeFaviconToDb(const QDir    &workingDir,
                      const QString &filePath,
                      const QString &mimeType,
                      const QString &canonicalName)
{
    QFile f(filePath);
    if (!f.open(QFile::ReadOnly)) {
        return false;
    }
    const QByteArray bytes = f.readAll();
    if (bytes.isEmpty()) {
        return false;
    }
    ImageWriter writer(workingDir);
    const qint64 id = writer.writeImage(bytes, mimeType);
    if (id < 0) {
        return false;
    }
    // domain="" is the global wildcard fallback in ImageRepositorySQLite.
    writer.linkName(id, QString(), canonicalName);
    return true;
}

} // namespace

PaneSettings::PaneSettings(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneSettings)
{
    ui->setupUi(this);
    ui->tableViewSettings->setItemDelegate(new SettingsDelegate(this));

    const auto &themes = AbstractTheme::ALL_THEMES();

    // Hide the theme row when there is only one theme — no choice to offer.
    if (themes.size() <= 1) {
        ui->widgetThemeRow->setVisible(false);
    } else {
        for (const AbstractTheme *theme : std::as_const(themes)) {
            ui->comboTheme->addItem(theme->getName(), theme->getId());
        }

        // Restore the saved selection.
        const auto settings = WorkingDirectoryManager::instance()->settings();
        const QString savedId = settings->value(AbstractTheme::settingsKey()).toString();
        const int savedIndex  = ui->comboTheme->findData(savedId);
        if (savedIndex >= 0) {
            ui->comboTheme->setCurrentIndex(savedIndex);
        }

        connect(ui->comboTheme, &QComboBox::currentIndexChanged,
                this, &PaneSettings::onThemeChanged);
    }

    // CLI availability table (all CLIs + async status) and filtered list (available only).
    m_cliTable = new AvailableCliTable(this);
    m_cliList  = new AvailableCliList(m_cliTable, this);
    ui->tableViewCli->setModel(m_cliTable);
    ui->comboDefaultCli->setModel(m_cliList);

    // Restore saved selection now and again each time a new CLI becomes available.
    connect(m_cliList, &QAbstractListModel::rowsInserted, this,
            [this](const QModelIndex &, int, int) { _restoreDefaultCli(); });
    _restoreDefaultCli();

    connect(ui->comboDefaultCli, &QComboBox::currentIndexChanged,
            this, &PaneSettings::onDefaultCliChanged);

    // Favicon buttons
    connect(ui->pushButtonPickFaviconSvg,   &QPushButton::clicked, this, &PaneSettings::onPickFaviconSvg);
    connect(ui->pushButtonClearFaviconSvg,  &QPushButton::clicked, this, &PaneSettings::onClearFaviconSvg);
    connect(ui->pushButtonPickFaviconIco,   &QPushButton::clicked, this, &PaneSettings::onPickFaviconIco);
    connect(ui->pushButtonClearFaviconIco,  &QPushButton::clicked, this, &PaneSettings::onClearFaviconIco);
    connect(ui->pushButtonPickFaviconApple, &QPushButton::clicked, this, &PaneSettings::onPickFaviconApple);
    connect(ui->pushButtonClearFaviconApple,&QPushButton::clicked, this, &PaneSettings::onClearFaviconApple);

    _updateFaviconLabels();
}

PaneSettings::~PaneSettings()
{
    delete ui;
}

void PaneSettings::setSettingsTable(WebsiteSettingsTable *settingsTable)
{
    ui->tableViewSettings->setModel(settingsTable);
}

void PaneSettings::setWorkingDir(const QDir &dir)
{
    m_workingDir = dir;
    _updateFaviconLabels();
}

void PaneSettings::setTheme(AbstractTheme *theme)
{
    m_theme = theme;
    _updateFaviconLabels();
}

void PaneSettings::onThemeChanged(int index)
{
    const QString themeId = ui->comboTheme->itemData(index).toString();
    const auto settings   = WorkingDirectoryManager::instance()->settings();
    settings->setValue(AbstractTheme::settingsKey(), themeId);
}

void PaneSettings::onDefaultCliChanged(int index)
{
    AbstractCli *cli = m_cliList->cliAt(index);
    const QString name = cli ? cli->getName() : QString{};
    WorkingDirectoryManager::instance()->settings()
        ->setValue(QStringLiteral("defaultCli"), name);
}

void PaneSettings::_restoreDefaultCli()
{
    const QString saved = WorkingDirectoryManager::instance()->settings()
                              ->value(QStringLiteral("defaultCli")).toString();
    if (saved.isEmpty()) {
        return;
    }
    for (int i = 0; i < m_cliList->rowCount(); ++i) {
        const QString name =
            m_cliList->data(m_cliList->index(i, 0), Qt::DisplayRole).toString();
        if (name == saved) {
            const QSignalBlocker blocker(ui->comboDefaultCli);
            ui->comboDefaultCli->setCurrentIndex(i);
            return;
        }
    }
}

void PaneSettings::_updateFaviconLabels()
{
    const auto label = [](const QString &filename) -> QString {
        return filename.isEmpty() ? QStringLiteral("—") : filename;
    };
    if (m_theme) {
        ui->labelFaviconSvg->setText(label(m_theme->faviconSvg()));
        ui->labelFaviconIco->setText(label(m_theme->faviconIco()));
        ui->labelFaviconApple->setText(label(m_theme->faviconAppleTouch()));
    } else {
        ui->labelFaviconSvg->setText(QStringLiteral("—"));
        ui->labelFaviconIco->setText(QStringLiteral("—"));
        ui->labelFaviconApple->setText(QStringLiteral("—"));
    }
    const bool enabled = m_theme != nullptr && m_workingDir.exists();
    ui->pushButtonPickFaviconSvg->setEnabled(enabled);
    ui->pushButtonPickFaviconIco->setEnabled(enabled);
    ui->pushButtonPickFaviconApple->setEnabled(enabled);
}

// ---------------------------------------------------------------------------
// Favicon slots
// ---------------------------------------------------------------------------

void PaneSettings::onPickFaviconSvg()
{
    if (!m_theme) { return; }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Pick SVG Favicon"), {}, tr("SVG files (*.svg)"));
    if (path.isEmpty()) { return; }
    if (!writeFaviconToDb(m_workingDir, path,
                          QStringLiteral("image/svg+xml"),
                          QStringLiteral("favicon.svg"))) {
        QMessageBox::warning(this, tr("Favicon"), tr("Failed to read or write the selected file."));
        return;
    }
    m_theme->setFaviconSvg(QStringLiteral("favicon.svg"));
    _updateFaviconLabels();
}

void PaneSettings::onClearFaviconSvg()
{
    if (!m_theme) { return; }
    m_theme->setFaviconSvg({});
    _updateFaviconLabels();
}

void PaneSettings::onPickFaviconIco()
{
    if (!m_theme) { return; }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Pick ICO Favicon"), {}, tr("ICO files (*.ico)"));
    if (path.isEmpty()) { return; }
    if (!writeFaviconToDb(m_workingDir, path,
                          QStringLiteral("image/x-icon"),
                          QStringLiteral("favicon.ico"))) {
        QMessageBox::warning(this, tr("Favicon"), tr("Failed to read or write the selected file."));
        return;
    }
    m_theme->setFaviconIco(QStringLiteral("favicon.ico"));
    _updateFaviconLabels();
}

void PaneSettings::onClearFaviconIco()
{
    if (!m_theme) { return; }
    m_theme->setFaviconIco({});
    _updateFaviconLabels();
}

void PaneSettings::onPickFaviconApple()
{
    if (!m_theme) { return; }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Pick Apple Touch Icon (180×180 PNG)"), {}, tr("PNG files (*.png)"));
    if (path.isEmpty()) { return; }
    if (!writeFaviconToDb(m_workingDir, path,
                          QStringLiteral("image/png"),
                          QStringLiteral("apple-touch-icon.png"))) {
        QMessageBox::warning(this, tr("Favicon"), tr("Failed to read or write the selected file."));
        return;
    }
    m_theme->setFaviconAppleTouch(QStringLiteral("apple-touch-icon.png"));
    _updateFaviconLabels();
}

void PaneSettings::onClearFaviconApple()
{
    if (!m_theme) { return; }
    m_theme->setFaviconAppleTouch({});
    _updateFaviconLabels();
}
