#include "DialogViewUpdatedArticles.h"
#include "ui_DialogViewUpdatedArticles.h"

#include "website/pages/PageRecord.h"

DialogViewUpdatedArticles::DialogViewUpdatedArticles(const QList<PageRecord> &updated,
                                                      const QList<PageRecord> &notUpdated,
                                                      QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogViewUpdatedArticles)
{
    ui->setupUi(this);

    ui->labelUpdated->setText(tr("Updated (%1)").arg(updated.size()));
    for (const PageRecord &page : std::as_const(updated)) {
        ui->listUpdated->addItem(page.permalink);
    }

    ui->labelNotUpdated->setText(tr("Not updated (%1)").arg(notUpdated.size()));
    for (const PageRecord &page : std::as_const(notUpdated)) {
        ui->listNotUpdated->addItem(page.permalink);
    }
}

DialogViewUpdatedArticles::~DialogViewUpdatedArticles()
{
    delete ui;
}
