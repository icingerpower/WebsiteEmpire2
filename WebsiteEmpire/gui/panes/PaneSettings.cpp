#include "PaneSettings.h"
#include "ui_PaneSettings.h"

#include "website/WebsiteSettingsTable.h"

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
}

PaneSettings::~PaneSettings()
{
    delete ui;
}

void PaneSettings::setSettingsTable(WebsiteSettingsTable *settingsTable)
{
    ui->tableViewSettings->setModel(settingsTable);
}
