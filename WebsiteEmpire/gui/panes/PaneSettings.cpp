#include "PaneSettings.h"
#include "ui_PaneSettings.h"

#include "website/WebsiteSettingsTable.h"
#include "website/theme/AbstractTheme.h"
#include "workingdirectory/WorkingDirectoryManager.h"

#include <QAbstractItemModel>
#include <QComboBox>
#include <QLocale>
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
                                       : QStringLiteral("%1 \u2014 %2").arg(code, native);
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
}

PaneSettings::~PaneSettings()
{
    delete ui;
}

void PaneSettings::setSettingsTable(WebsiteSettingsTable *settingsTable)
{
    ui->tableViewSettings->setModel(settingsTable);
}

void PaneSettings::onThemeChanged(int index)
{
    const QString themeId = ui->comboTheme->itemData(index).toString();
    const auto settings   = WorkingDirectoryManager::instance()->settings();
    settings->setValue(AbstractTheme::settingsKey(), themeId);
}
