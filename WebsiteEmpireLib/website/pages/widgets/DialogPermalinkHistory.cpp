#include "DialogPermalinkHistory.h"
#include "ui_DialogPermalinkHistory.h"

#include "website/pages/IPageRepository.h"
#include "website/pages/PermalinkHistoryEntry.h"

#include <QComboBox>

DialogPermalinkHistory::DialogPermalinkHistory(IPageRepository &repo,
                                               int              pageId,
                                               QWidget         *parent)
    : QDialog(parent)
    , ui(new Ui::DialogPermalinkHistory)
    , m_repo(repo)
    , m_pageId(pageId)
{
    ui->setupUi(this);
    setWindowTitle(tr("Permalink History"));
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    _populate();
}

DialogPermalinkHistory::~DialogPermalinkHistory()
{
    delete ui;
}

void DialogPermalinkHistory::_populate()
{
    const QList<PermalinkHistoryEntry> &history = m_repo.permalinkHistory(m_pageId);

    ui->tableWidget->setRowCount(history.size());
    ui->tableWidget->setColumnCount(3);
    ui->tableWidget->setHorizontalHeaderLabels(
        {tr("Old Permalink"), tr("Changed At"), tr("Redirect Type")});

    // Map readable labels to stored values.
    const QStringList typeValues  = {QStringLiteral("permanent"),
                                     QStringLiteral("parked"),
                                     QStringLiteral("deleted")};
    const QStringList typeLabels  = {tr("Permanent 301"),
                                     tr("Parked 302"),
                                     tr("Deleted 410")};

    for (int row = 0; row < history.size(); ++row) {
        const PermalinkHistoryEntry &e = history.at(row);

        auto *itemPermalink  = new QTableWidgetItem(e.permalink);
        auto *itemChangedAt  = new QTableWidgetItem(e.changedAt);
        itemPermalink->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        itemChangedAt->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        ui->tableWidget->setItem(row, 0, itemPermalink);
        ui->tableWidget->setItem(row, 1, itemChangedAt);

        auto *combo = new QComboBox(ui->tableWidget);
        combo->addItems(typeLabels);
        const int idx = typeValues.indexOf(e.redirectType);
        combo->setCurrentIndex(idx >= 0 ? idx : 0);

        // Save immediately when the user changes the selection.
        const int entryId = e.id;
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this, combo, typeValues, entryId](int index) {
                    if (index >= 0 && index < typeValues.size()) {
                        m_repo.setHistoryRedirectType(entryId, typeValues.at(index));
                    }
                });

        ui->tableWidget->setCellWidget(row, 2, combo);
    }

    ui->tableWidget->resizeColumnsToContents();
    ui->tableWidget->horizontalHeader()->setStretchLastSection(true);
}
