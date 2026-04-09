#include "PaneTheme.h"
#include "ui_PaneTheme.h"

#include "widgets/WidgetEditCommonBlocs.h"
#include "website/commonblocs/AbstractCommonBloc.h"
#include "website/theme/AbstractTheme.h"

#include <QColorDialog>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QStyledItemDelegate>

// ---------------------------------------------------------------------------
// Delegate — opens QColorDialog for colour params, text editor for others
// ---------------------------------------------------------------------------

class ThemeParamsDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        if (index.data(AbstractTheme::IsColorRole).toBool()) {
            return nullptr; // colour pick is handled in editorEvent
        }
        return QStyledItemDelegate::createEditor(parent, option, index);
    }

    bool editorEvent(QEvent *event,
                     QAbstractItemModel *model,
                     const QStyleOptionViewItem &option,
                     const QModelIndex &index) override
    {
        if (index.data(AbstractTheme::IsColorRole).toBool()
                && event->type() == QEvent::MouseButtonDblClick) {
            const QColor current =
                QColor::fromString(index.data(Qt::EditRole).toString());
            const QColor chosen = QColorDialog::getColor(
                current,
                qobject_cast<QWidget *>(parent()),
                QObject::tr("Pick Color"));
            if (chosen.isValid()) {
                model->setData(index, chosen.name(), Qt::EditRole);
            }
            return true;
        }
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }
};

PaneTheme::PaneTheme(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneTheme)
{
    ui->setupUi(this);
    _populateThemeList();

    ui->tableViewParams->setItemDelegateForColumn(
        AbstractTheme::COL_VALUE, new ThemeParamsDelegate(ui->tableViewParams));

    connect(ui->listWidgetThemes, &QListWidget::currentItemChanged,
            this, &PaneTheme::onCurrentThemeItemChanged);
}

PaneTheme::~PaneTheme()
{
    delete ui;
}

void PaneTheme::setTheme(AbstractTheme *theme)
{
    m_theme = theme;

    // Bind the params table to the theme model.
    ui->tableViewParams->setModel(theme);

    // Rebuild the common-bloc toolbox for the new theme.
    _rebuildBlocsToolBox();

    // Sync the list selection to the new theme without triggering the
    // confirmation dialog (disconnect / reconnect the signal).
    if (!theme) {
        return;
    }

    disconnect(ui->listWidgetThemes, &QListWidget::currentItemChanged,
               this, &PaneTheme::onCurrentThemeItemChanged);

    for (int i = 0; i < ui->listWidgetThemes->count(); ++i) {
        QListWidgetItem *item = ui->listWidgetThemes->item(i);
        if (item->data(Qt::UserRole).toString() == theme->getId()) {
            ui->listWidgetThemes->setCurrentItem(item);
            break;
        }
    }

    connect(ui->listWidgetThemes, &QListWidget::currentItemChanged,
            this, &PaneTheme::onCurrentThemeItemChanged);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void PaneTheme::_populateThemeList()
{
    ui->listWidgetThemes->clear();

    const auto &themes = AbstractTheme::ALL_THEMES();
    for (auto it = themes.cbegin(); it != themes.cend(); ++it) {
        const AbstractTheme *proto = it.value();
        auto *item = new QListWidgetItem(proto->getName(), ui->listWidgetThemes);
        item->setData(Qt::UserRole, proto->getId());
    }
}

void PaneTheme::_rebuildBlocsToolBox()
{
    // Remove all existing pages.
    while (ui->toolBoxBlocs->count() > 0) {
        QWidget *page = ui->toolBoxBlocs->widget(0);
        ui->toolBoxBlocs->removeItem(0);
        delete page;
    }

    if (!m_theme) {
        return;
    }

    auto addBlocsFromList = [this](const QList<AbstractCommonBloc *> &blocs) {
        for (AbstractCommonBloc *bloc : blocs) {
            auto *editor = new WidgetEditCommonBlocs(ui->toolBoxBlocs);
            editor->setBloc(bloc, m_theme);
            ui->toolBoxBlocs->addItem(editor, bloc->getName());
        }
    };

    addBlocsFromList(m_theme->getTopBlocs());
    addBlocsFromList(m_theme->getBottomBlocs());
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void PaneTheme::onCurrentThemeItemChanged(QListWidgetItem *current,
                                          QListWidgetItem *previous)
{
    if (!current) {
        return;
    }

    const QString newId = current->data(Qt::UserRole).toString();
    const QString currentId = m_theme ? m_theme->getId() : QString();

    if (newId == currentId) {
        return;
    }

    const int answer = QMessageBox::question(
        this,
        tr("Change Theme"),
        tr("Switch to theme \"%1\"?\n\nUnsaved changes to the current theme will be lost.")
            .arg(current->text()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (answer == QMessageBox::Yes) {
        emit themeIdSelected(newId);
    } else {
        // Revert the selection without triggering this slot again.
        disconnect(ui->listWidgetThemes, &QListWidget::currentItemChanged,
                   this, &PaneTheme::onCurrentThemeItemChanged);
        ui->listWidgetThemes->setCurrentItem(previous);
        connect(ui->listWidgetThemes, &QListWidget::currentItemChanged,
                this, &PaneTheme::onCurrentThemeItemChanged);
    }
}
