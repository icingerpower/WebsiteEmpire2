#include "PageBlocCategoryArticlesWidget.h"
#include "ui_PageBlocCategoryArticlesWidget.h"

#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/PageBlocCategoryArticles.h"

#include <QComboBox>
#include <QHeaderView>
#include <QSpinBox>

#include <algorithm>

// =============================================================================
// Construction / Destruction
// =============================================================================

PageBlocCategoryArticlesWidget::PageBlocCategoryArticlesWidget(
        CategoryTable &categoryTable, QWidget *parent)
    : AbstractPageBlockWidget(parent)
    , ui(new Ui::PageBlocCategoryArticlesWidget)
    , m_categoryTable(categoryTable)
{
    ui->setupUi(this);

    ui->tableEntries->horizontalHeader()->setStretchLastSection(true);

    connect(ui->btnAdd, &QPushButton::clicked, this, [this]() {
        addRow();
    });

    connect(ui->btnRemove, &QPushButton::clicked, this, [this]() {
        const auto &selected = ui->tableEntries->selectionModel()->selectedRows();
        // Remove from bottom to top so indices stay valid.
        QList<int> rows;
        rows.reserve(selected.size());
        for (const auto &idx : selected) {
            rows.append(idx.row());
        }
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int row : std::as_const(rows)) {
            ui->tableEntries->removeRow(row);
        }
    });
}

PageBlocCategoryArticlesWidget::~PageBlocCategoryArticlesWidget()
{
    delete ui;
}

// =============================================================================
// load / save
// =============================================================================

void PageBlocCategoryArticlesWidget::load(const QHash<QString, QString> &values)
{
    const int count = values.value(
        QLatin1String(PageBlocCategoryArticles::KEY_ENTRY_COUNT),
        QStringLiteral("0")).toInt();

    ui->tableEntries->setRowCount(0);
    for (int i = 0; i < count; ++i) {
        const int row = addRow();
        const auto &prefix = QStringLiteral("entry_") + QString::number(i) + QStringLiteral("_");

        // Category combo (column 0).
        const int catId = values.value(prefix + QStringLiteral("cat_id"),
                                       QStringLiteral("0")).toInt();
        auto *catCombo = qobject_cast<QComboBox *>(ui->tableEntries->cellWidget(row, 0));
        if (catCombo) {
            const int idx = catCombo->findData(catId);
            if (idx >= 0) {
                catCombo->setCurrentIndex(idx);
            }
        }

        // Title (column 1).
        ui->tableEntries->item(row, 1)->setText(
            values.value(prefix + QStringLiteral("title")));

        // Articles spin box (column 2).
        auto *spin = qobject_cast<QSpinBox *>(ui->tableEntries->cellWidget(row, 2));
        if (spin) {
            spin->setValue(values.value(prefix + QStringLiteral("item_count"),
                                       QStringLiteral("5")).toInt());
        }

        // Sort combo (column 3).
        const auto &sortVal = values.value(prefix + QStringLiteral("sort"),
                                           QLatin1String(PageBlocCategoryArticles::SORT_RECENT));
        auto *sortCombo = qobject_cast<QComboBox *>(ui->tableEntries->cellWidget(row, 3));
        if (sortCombo) {
            const int idx = sortCombo->findData(sortVal);
            if (idx >= 0) {
                sortCombo->setCurrentIndex(idx);
            }
        }
    }
}

void PageBlocCategoryArticlesWidget::save(QHash<QString, QString> &values) const
{
    const int count = ui->tableEntries->rowCount();
    values.insert(QLatin1String(PageBlocCategoryArticles::KEY_ENTRY_COUNT),
                  QString::number(count));

    for (int i = 0; i < count; ++i) {
        const auto &prefix = QStringLiteral("entry_") + QString::number(i) + QStringLiteral("_");

        // Category combo (column 0).
        auto *catCombo = qobject_cast<QComboBox *>(ui->tableEntries->cellWidget(i, 0));
        values.insert(prefix + QStringLiteral("cat_id"),
                      catCombo ? QString::number(catCombo->currentData().toInt())
                               : QStringLiteral("0"));

        // Title (column 1).
        const auto *titleItem = ui->tableEntries->item(i, 1);
        values.insert(prefix + QStringLiteral("title"),
                      titleItem ? titleItem->text() : QString());

        // Articles spin box (column 2).
        auto *spin = qobject_cast<QSpinBox *>(ui->tableEntries->cellWidget(i, 2));
        values.insert(prefix + QStringLiteral("item_count"),
                      spin ? QString::number(spin->value()) : QStringLiteral("5"));

        // Sort combo (column 3).
        auto *sortCombo = qobject_cast<QComboBox *>(ui->tableEntries->cellWidget(i, 3));
        values.insert(prefix + QStringLiteral("sort"),
                      sortCombo ? sortCombo->currentData().toString()
                                : QLatin1String(PageBlocCategoryArticles::SORT_RECENT));
    }
}

// =============================================================================
// addRow  (private)
// =============================================================================

int PageBlocCategoryArticlesWidget::addRow()
{
    const int row = ui->tableEntries->rowCount();
    ui->tableEntries->insertRow(row);

    // Column 0: QComboBox for category.
    auto *catCombo = new QComboBox;
    const auto &categories = m_categoryTable.categories();
    for (const auto &cat : categories) {
        catCombo->addItem(cat.name, cat.id);
    }
    ui->tableEntries->setCellWidget(row, 0, catCombo);

    // Column 1: plain editable text item (title).
    ui->tableEntries->setItem(row, 1, new QTableWidgetItem);

    // Column 2: QSpinBox for article count.
    auto *spin = new QSpinBox;
    spin->setMinimum(1);
    spin->setMaximum(50);
    spin->setValue(5);
    ui->tableEntries->setCellWidget(row, 2, spin);

    // Column 3: QComboBox for sort order.
    auto *sortCombo = new QComboBox;
    sortCombo->addItem(tr("Recent"),
                       QLatin1String(PageBlocCategoryArticles::SORT_RECENT));
    sortCombo->addItem(tr("Performance"),
                       QLatin1String(PageBlocCategoryArticles::SORT_PERFORMANCE));
    sortCombo->addItem(tr("Combined"),
                       QLatin1String(PageBlocCategoryArticles::SORT_COMBINED));
    ui->tableEntries->setCellWidget(row, 3, sortCombo);

    return row;
}
