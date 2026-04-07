#include "PageBlocImageLinksWidget.h"
#include "ui_PageBlocImageLinksWidget.h"

#include "website/pages/blocs/PageBlocImageLinks.h"

#include <QComboBox>
#include <QHeaderView>

// =============================================================================
// Construction / Destruction
// =============================================================================

PageBlocImageLinksWidget::PageBlocImageLinksWidget(QWidget *parent)
    : AbstractPageBlockWidget(parent)
    , ui(new Ui::PageBlocImageLinksWidget)
{
    ui->setupUi(this);

    // QHeaderView::Stretch on the last section (in case .ui cannot express it).
    ui->tableItems->horizontalHeader()->setStretchLastSection(true);

    connect(ui->btnAdd, &QPushButton::clicked, this, [this]() {
        addRow();
    });

    connect(ui->btnRemove, &QPushButton::clicked, this, [this]() {
        const auto &selected = ui->tableItems->selectionModel()->selectedRows();
        // Remove from bottom to top so indices stay valid.
        QList<int> rows;
        rows.reserve(selected.size());
        for (const auto &idx : selected) {
            rows.append(idx.row());
        }
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int row : std::as_const(rows)) {
            ui->tableItems->removeRow(row);
        }
    });
}

PageBlocImageLinksWidget::~PageBlocImageLinksWidget()
{
    delete ui;
}

// =============================================================================
// load / save
// =============================================================================

void PageBlocImageLinksWidget::load(const QHash<QString, QString> &values)
{
    ui->spinColsDesktop->setValue(
        values.value(QLatin1String(PageBlocImageLinks::KEY_COLS_DESKTOP), QStringLiteral("4")).toInt());
    ui->spinRowsDesktop->setValue(
        values.value(QLatin1String(PageBlocImageLinks::KEY_ROWS_DESKTOP), QStringLiteral("2")).toInt());
    ui->spinColsTablet->setValue(
        values.value(QLatin1String(PageBlocImageLinks::KEY_COLS_TABLET), QStringLiteral("2")).toInt());
    ui->spinRowsTablet->setValue(
        values.value(QLatin1String(PageBlocImageLinks::KEY_ROWS_TABLET), QStringLiteral("3")).toInt());
    ui->spinColsMobile->setValue(
        values.value(QLatin1String(PageBlocImageLinks::KEY_COLS_MOBILE), QStringLiteral("1")).toInt());
    ui->spinRowsMobile->setValue(
        values.value(QLatin1String(PageBlocImageLinks::KEY_ROWS_MOBILE), QStringLiteral("4")).toInt());

    const int count = values.value(QLatin1String(PageBlocImageLinks::KEY_ITEM_COUNT),
                                   QStringLiteral("0")).toInt();

    ui->tableItems->setRowCount(0);
    for (int i = 0; i < count; ++i) {
        const int row = addRow();
        const auto &prefix = QStringLiteral("item_") + QString::number(i) + QStringLiteral("_");

        ui->tableItems->item(row, 0)->setText(
            values.value(prefix + QStringLiteral("img")));

        // Set the combo box to the matching link type.
        const auto &linkType = values.value(prefix + QStringLiteral("type"));
        auto *combo = qobject_cast<QComboBox *>(ui->tableItems->cellWidget(row, 1));
        if (combo) {
            const int idx = combo->findData(linkType);
            if (idx >= 0) {
                combo->setCurrentIndex(idx);
            }
        }

        ui->tableItems->item(row, 2)->setText(
            values.value(prefix + QStringLiteral("target")));
        ui->tableItems->item(row, 3)->setText(
            values.value(prefix + QStringLiteral("alt")));
    }
}

void PageBlocImageLinksWidget::save(QHash<QString, QString> &values) const
{
    values.insert(QLatin1String(PageBlocImageLinks::KEY_COLS_DESKTOP),
                  QString::number(ui->spinColsDesktop->value()));
    values.insert(QLatin1String(PageBlocImageLinks::KEY_ROWS_DESKTOP),
                  QString::number(ui->spinRowsDesktop->value()));
    values.insert(QLatin1String(PageBlocImageLinks::KEY_COLS_TABLET),
                  QString::number(ui->spinColsTablet->value()));
    values.insert(QLatin1String(PageBlocImageLinks::KEY_ROWS_TABLET),
                  QString::number(ui->spinRowsTablet->value()));
    values.insert(QLatin1String(PageBlocImageLinks::KEY_COLS_MOBILE),
                  QString::number(ui->spinColsMobile->value()));
    values.insert(QLatin1String(PageBlocImageLinks::KEY_ROWS_MOBILE),
                  QString::number(ui->spinRowsMobile->value()));

    const int count = ui->tableItems->rowCount();
    values.insert(QLatin1String(PageBlocImageLinks::KEY_ITEM_COUNT),
                  QString::number(count));

    for (int i = 0; i < count; ++i) {
        const auto &prefix = QStringLiteral("item_") + QString::number(i) + QStringLiteral("_");

        const auto *imgItem    = ui->tableItems->item(i, 0);
        const auto *targetItem = ui->tableItems->item(i, 2);
        const auto *altItem    = ui->tableItems->item(i, 3);

        values.insert(prefix + QStringLiteral("img"),
                      imgItem ? imgItem->text() : QString());
        values.insert(prefix + QStringLiteral("target"),
                      targetItem ? targetItem->text() : QString());
        values.insert(prefix + QStringLiteral("alt"),
                      altItem ? altItem->text() : QString());

        auto *combo = qobject_cast<QComboBox *>(ui->tableItems->cellWidget(i, 1));
        values.insert(prefix + QStringLiteral("type"),
                      combo ? combo->currentData().toString() : QString());
    }
}

// =============================================================================
// addRow  (private)
// =============================================================================

int PageBlocImageLinksWidget::addRow()
{
    const int row = ui->tableItems->rowCount();
    ui->tableItems->insertRow(row);

    // Columns 0, 2, 3 are plain text items.
    ui->tableItems->setItem(row, 0, new QTableWidgetItem);
    ui->tableItems->setItem(row, 2, new QTableWidgetItem);
    ui->tableItems->setItem(row, 3, new QTableWidgetItem);

    // Column 1: QComboBox for link type.
    auto *combo = new QComboBox;
    combo->addItem(tr("Category"), QLatin1String(PageBlocImageLinks::LINK_TYPE_CATEGORY));
    combo->addItem(tr("Page"),     QLatin1String(PageBlocImageLinks::LINK_TYPE_PAGE));
    combo->addItem(tr("URL"),      QLatin1String(PageBlocImageLinks::LINK_TYPE_URL));
    ui->tableItems->setCellWidget(row, 1, combo);

    return row;
}
