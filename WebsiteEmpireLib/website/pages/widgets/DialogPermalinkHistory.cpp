#include "DialogPermalinkHistory.h"
#include "PermalinkHistoryModel.h"
#include "ui_DialogPermalinkHistory.h"

#include "website/pages/IPageRepository.h"

#include <QComboBox>
#include <QHeaderView>
#include <QStyledItemDelegate>

// ---------------------------------------------------------------------------
// RedirectTypeDelegate — renders COL_REDIRECT as a combo box
// ---------------------------------------------------------------------------

class RedirectTypeDelegate : public QStyledItemDelegate
{
public:
    explicit RedirectTypeDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent) {}

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *combo = new QComboBox(parent);
        combo->addItems(PermalinkHistoryModel::redirectTypeLabels());
        return combo;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        const QString currentValue = index.data(Qt::EditRole).toString();
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo) {
            return;
        }
        const int idx = PermalinkHistoryModel::redirectTypeValues().indexOf(currentValue);
        combo->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    void setModelData(QWidget *editor,
                      QAbstractItemModel *model,
                      const QModelIndex  &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo) {
            return;
        }
        const int idx = combo->currentIndex();
        if (idx >= 0 && idx < PermalinkHistoryModel::redirectTypeValues().size()) {
            model->setData(index,
                           PermalinkHistoryModel::redirectTypeValues().at(idx),
                           Qt::EditRole);
        }
    }

    void updateEditorGeometry(QWidget *editor,
                              const QStyleOptionViewItem &option,
                              const QModelIndex &) const override
    {
        editor->setGeometry(option.rect);
    }
};

// ---------------------------------------------------------------------------
// DialogPermalinkHistory
// ---------------------------------------------------------------------------

DialogPermalinkHistory::DialogPermalinkHistory(IPageRepository &repo,
                                               int              pageId,
                                               QWidget         *parent)
    : QDialog(parent)
    , ui(new Ui::DialogPermalinkHistory)
    , m_model(new PermalinkHistoryModel(repo, pageId, this))
{
    ui->setupUi(this);
    setWindowTitle(tr("Permalink History"));
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    ui->tableWidget->setModel(m_model);
    ui->tableWidget->setItemDelegateForColumn(
        PermalinkHistoryModel::COL_REDIRECT,
        new RedirectTypeDelegate(ui->tableWidget));
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(
        PermalinkHistoryModel::COL_PERMALINK, QHeaderView::Stretch);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(
        PermalinkHistoryModel::COL_CHANGED_AT, QHeaderView::ResizeToContents);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(
        PermalinkHistoryModel::COL_REDIRECT, QHeaderView::ResizeToContents);
}

DialogPermalinkHistory::~DialogPermalinkHistory()
{
    delete ui;
}
