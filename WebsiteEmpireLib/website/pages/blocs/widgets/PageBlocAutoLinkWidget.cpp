#include "PageBlocAutoLinkWidget.h"
#include "ui_PageBlocAutoLinkWidget.h"

#include "website/pages/blocs/PageBlocAutoLink.h"

#include <QListWidgetItem>
#include <QPushButton>

PageBlocAutoLinkWidget::PageBlocAutoLinkWidget(QWidget *parent)
    : AbstractPageBlockWidget(parent)
    , ui(new Ui::PageBlocAutoLinkWidget)
{
    ui->setupUi(this);

    connect(ui->btnAdd, &QPushButton::clicked, this, [this]() {
        auto *item = new QListWidgetItem(QString{}, ui->listKeywords);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        ui->listKeywords->setCurrentItem(item);
        ui->listKeywords->editItem(item);
    });

    connect(ui->btnRemove, &QPushButton::clicked, this, [this]() {
        delete ui->listKeywords->currentItem();
    });
}

PageBlocAutoLinkWidget::~PageBlocAutoLinkWidget()
{
    delete ui;
}

void PageBlocAutoLinkWidget::load(const QHash<QString, QString> &values)
{
    // Round-trip the page URL so save() can write it back without modification.
    m_pageUrl = values.value(QLatin1String(PageBlocAutoLink::KEY_PAGE_URL));

    ui->listKeywords->clear();
    const int count = values.value(QLatin1String(PageBlocAutoLink::KEY_KW_COUNT)).toInt();
    for (int i = 0; i < count; ++i) {
        const QString &kw = values.value(QStringLiteral("kw_") + QString::number(i));
        if (!kw.isEmpty()) {
            auto *item = new QListWidgetItem(kw, ui->listKeywords);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        }
    }
}

void PageBlocAutoLinkWidget::save(QHash<QString, QString> &values) const
{
    // Collect non-empty keywords in order, skipping rows left blank.
    QStringList keywords;
    for (int i = 0; i < ui->listKeywords->count(); ++i) {
        const QString kw = ui->listKeywords->item(i)->text().trimmed();
        if (!kw.isEmpty()) {
            keywords.append(kw);
        }
    }

    values.insert(QLatin1String(PageBlocAutoLink::KEY_PAGE_URL), m_pageUrl);
    values.insert(QLatin1String(PageBlocAutoLink::KEY_KW_COUNT), QString::number(keywords.size()));
    for (int i = 0; i < keywords.size(); ++i) {
        values.insert(QStringLiteral("kw_") + QString::number(i), keywords.at(i));
    }
}
