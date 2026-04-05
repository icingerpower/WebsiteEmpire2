#include "DialogPreviewPage.h"
#include "ui_DialogPreviewPage.h"

#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/attributes/CategoryTable.h"

#include <QSet>

DialogPreviewPage::DialogPreviewPage(IPageRepository &repo,
                                     CategoryTable   &categoryTable,
                                     AbstractEngine  &engine,
                                     int              pageId,
                                     QWidget         *parent)
    : QDialog(parent)
    , ui(new Ui::DialogPreviewPage)
{
    ui->setupUi(this);

    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const auto rec = repo.findById(pageId);
    if (!rec) {
        ui->textBrowser->setPlainText(tr("Page not found."));
        return;
    }

    auto type = AbstractPageType::createForTypeId(rec->typeId, categoryTable);
    if (!type) {
        ui->textBrowser->setPlainText(tr("Unknown page type: %1").arg(rec->typeId));
        return;
    }

    const QHash<QString, QString> &data = repo.loadData(pageId);
    type->load(data);

    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    type->addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);

    setWindowTitle(tr("Preview — %1").arg(rec->permalink));
    ui->textBrowser->setHtml(html);
}

DialogPreviewPage::~DialogPreviewPage()
{
    delete ui;
}
