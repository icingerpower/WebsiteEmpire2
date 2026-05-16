#include "DialogGeneratePhase2.h"
#include "ui_DialogGeneratePhase2.h"

#include <algorithm>

#include <QListWidgetItem>
#include <QPushButton>
#include <QSet>

// UserRole data stored on each QListWidgetItem.
static constexpr int kRolePageId    = Qt::UserRole;
static constexpr int kRolePermalink = Qt::UserRole + 1;
static constexpr int kRoleTypeId    = Qt::UserRole + 2;

// comboBoxSort data values identifying each sort mode.
static const QString kSortById       = QStringLiteral("id");
static const QString kSortByPermalink = QStringLiteral("permalink");
static const QString kSortByStrategy  = QStringLiteral("strategy");

DialogGeneratePhase2::DialogGeneratePhase2(const QList<PageRecord> &pages,
                                            QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogGeneratePhase2)
    , m_pages(pages)
{
    ui->setupUi(this);

    // Sort combo — always offer ID and Permalink; Strategy only when pages span > 1 typeId.
    ui->comboBoxSort->addItem(tr("By ID"),        kSortById);
    ui->comboBoxSort->addItem(tr("By Permalink"), kSortByPermalink);
    QSet<QString> seenTypes;
    for (const PageRecord &p : std::as_const(m_pages)) {
        seenTypes.insert(p.typeId);
    }
    if (seenTypes.size() > 1) {
        ui->comboBoxSort->addItem(tr("By Strategy"), kSortByStrategy);
    }

    // Type filter combo — "All types" first, then one entry per distinct typeId.
    ui->comboBoxType->addItem(tr("All types"), QString{});
    for (const QString &typeId : std::as_const(seenTypes)) {
        ui->comboBoxType->addItem(typeId, typeId);
    }

    // Populate list (m_checkStates is empty → all items default to Checked).
    _rebuildList();
    _updateCount();

    connect(ui->buttonCheckAll,   &QPushButton::clicked,
            this, &DialogGeneratePhase2::_checkAll);
    connect(ui->buttonUncheckAll, &QPushButton::clicked,
            this, &DialogGeneratePhase2::_uncheckAll);
    connect(ui->lineEditFilter,   &QLineEdit::textChanged,
            this, &DialogGeneratePhase2::_applyFilter);
    connect(ui->comboBoxType,     QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DialogGeneratePhase2::_applyFilter);
    connect(ui->comboBoxSort,     QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DialogGeneratePhase2::_applySort);
    connect(ui->listWidget, &QListWidget::itemChanged,
            this, &DialogGeneratePhase2::_updateCount);
}

DialogGeneratePhase2::~DialogGeneratePhase2()
{
    delete ui;
}

QList<int> DialogGeneratePhase2::selectedPageIds() const
{
    QList<int> result;
    const int n = ui->listWidget->count();
    for (int i = 0; i < n; ++i) {
        const QListWidgetItem *item = ui->listWidget->item(i);
        if (!item->isHidden() && item->checkState() == Qt::Checked) {
            result.append(item->data(kRolePageId).toInt());
        }
    }
    return result;
}

void DialogGeneratePhase2::_checkAll()
{
    const int n = ui->listWidget->count();
    for (int i = 0; i < n; ++i) {
        QListWidgetItem *item = ui->listWidget->item(i);
        if (!item->isHidden()) {
            item->setCheckState(Qt::Checked);
        }
    }
}

void DialogGeneratePhase2::_uncheckAll()
{
    const int n = ui->listWidget->count();
    for (int i = 0; i < n; ++i) {
        QListWidgetItem *item = ui->listWidget->item(i);
        if (!item->isHidden()) {
            item->setCheckState(Qt::Unchecked);
        }
    }
}

void DialogGeneratePhase2::_applyFilter()
{
    const QString text       = ui->lineEditFilter->text().trimmed().toLower();
    const QString typeFilter = ui->comboBoxType->currentData().toString();

    const int n = ui->listWidget->count();
    for (int i = 0; i < n; ++i) {
        QListWidgetItem *item = ui->listWidget->item(i);
        const QString &permalink = item->data(kRolePermalink).toString();
        const QString &typeId    = item->data(kRoleTypeId).toString();

        const bool matchText = text.isEmpty() || permalink.contains(text, Qt::CaseInsensitive);
        const bool matchType = typeFilter.isEmpty() || typeId == typeFilter;
        item->setHidden(!matchText || !matchType);
    }

    _updateCount();
}

void DialogGeneratePhase2::_applySort()
{
    // Persist current check states before clearing the list.
    const int n = ui->listWidget->count();
    for (int i = 0; i < n; ++i) {
        const QListWidgetItem *item = ui->listWidget->item(i);
        m_checkStates[item->data(kRolePageId).toInt()] = item->checkState();
    }
    _rebuildList();
    _applyFilter();
}

void DialogGeneratePhase2::_updateCount()
{
    int selected = 0;
    int visible  = 0;
    const int n = ui->listWidget->count();
    for (int i = 0; i < n; ++i) {
        const QListWidgetItem *item = ui->listWidget->item(i);
        if (!item->isHidden()) {
            ++visible;
            if (item->checkState() == Qt::Checked) {
                ++selected;
            }
        }
    }
    ui->labelCount->setText(tr("%1 / %2 selected").arg(selected).arg(visible));
}

void DialogGeneratePhase2::_rebuildList()
{
    // Sort a copy of m_pages according to the current sort selection.
    QList<PageRecord> sorted = m_pages;
    const QString sortMode = ui->comboBoxSort->currentData().toString();
    if (sortMode == kSortByPermalink) {
        std::sort(sorted.begin(), sorted.end(),
                  [](const PageRecord &a, const PageRecord &b) {
                      return a.permalink < b.permalink;
                  });
    } else if (sortMode == kSortByStrategy) {
        std::sort(sorted.begin(), sorted.end(),
                  [](const PageRecord &a, const PageRecord &b) {
                      if (a.typeId != b.typeId) { return a.typeId < b.typeId; }
                      return a.id < b.id;
                  });
    } else {
        // kSortById (default)
        std::sort(sorted.begin(), sorted.end(),
                  [](const PageRecord &a, const PageRecord &b) {
                      return a.id < b.id;
                  });
    }

    // Rebuild the list widget, silencing itemChanged during population.
    disconnect(ui->listWidget, &QListWidget::itemChanged,
               this, &DialogGeneratePhase2::_updateCount);
    ui->listWidget->clear();
    for (const PageRecord &p : std::as_const(sorted)) {
        const QString label = QLatin1Char('#') + QString::number(p.id)
                            + QStringLiteral("  ") + p.permalink
                            + QStringLiteral("  [") + p.typeId + QLatin1Char(']');
        auto *item = new QListWidgetItem(label, ui->listWidget);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_checkStates.value(p.id, Qt::Checked));
        item->setData(kRolePageId,    p.id);
        item->setData(kRolePermalink, p.permalink);
        item->setData(kRoleTypeId,    p.typeId);
    }
    connect(ui->listWidget, &QListWidget::itemChanged,
            this, &DialogGeneratePhase2::_updateCount);
}
