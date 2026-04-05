#include "DialogPreviewPage.h"
#include "ui_DialogPreviewPage.h"

#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/attributes/CategoryTable.h"

#include <QListWidgetItem>
#include <QSet>

DialogPreviewPage::DialogPreviewPage(IPageRepository &repo,
                                     CategoryTable   &categoryTable,
                                     AbstractEngine  &engine,
                                     int              pageId,
                                     QWidget         *parent)
    : QDialog(parent)
    , ui(new Ui::DialogPreviewPage)
    , m_repo(repo)
    , m_categoryTable(categoryTable)
    , m_engine(engine)
{
    ui->setupUi(this);

    // Default splitter sizes: language list 150 px, browser takes the rest.
    ui->splitter->setSizes({150, 950});

    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(ui->listLanguages, &QListWidget::currentRowChanged,
            this, &DialogPreviewPage::_onLanguageSelected);

    // -------------------------------------------------------------------------
    // Build the list of pages to show (root + all translations).
    // -------------------------------------------------------------------------
    const auto &startRec = repo.findById(pageId);
    if (!startRec) {
        ui->textBrowser->setPlainText(tr("Page not found."));
        return;
    }

    // Resolve root page.
    const int rootId = (startRec->sourcePageId > 0) ? startRec->sourcePageId : pageId;

    // Add root page.
    const auto &rootRec = repo.findById(rootId);
    if (rootRec) {
        m_pageIds.append(rootRec->id);
        auto *item = new QListWidgetItem(rootRec->lang, ui->listLanguages);
        item->setToolTip(rootRec->permalink);
    }

    // Add translations.
    const QList<PageRecord> &translations = repo.findTranslations(rootId);
    for (const PageRecord &t : std::as_const(translations)) {
        m_pageIds.append(t.id);
        auto *item = new QListWidgetItem(t.lang, ui->listLanguages);
        item->setToolTip(t.permalink);
        // Append a lock indicator if not yet AI-translated.
        if (t.translatedAt.isEmpty()) {
            item->setText(t.lang + QStringLiteral(" [pending]"));
        }
    }

    // Select the row matching the originally requested pageId.
    const int row = m_pageIds.indexOf(pageId);
    if (row >= 0) {
        ui->listLanguages->setCurrentRow(row);
    } else if (!m_pageIds.isEmpty()) {
        ui->listLanguages->setCurrentRow(0);
    }
}

DialogPreviewPage::~DialogPreviewPage()
{
    delete ui;
}

void DialogPreviewPage::_onLanguageSelected(int row)
{
    if (row < 0 || row >= m_pageIds.size()) {
        return;
    }
    _renderPage(m_pageIds.at(row));
}

void DialogPreviewPage::_renderPage(int pageId)
{
    const auto &rec = m_repo.findById(pageId);
    if (!rec) {
        ui->textBrowser->setPlainText(tr("Page not found."));
        return;
    }

    auto type = AbstractPageType::createForTypeId(rec->typeId, m_categoryTable);
    if (!type) {
        ui->textBrowser->setPlainText(tr("Unknown page type: %1").arg(rec->typeId));
        return;
    }

    const QHash<QString, QString> &data = m_repo.loadData(pageId);
    type->load(data);

    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    type->addCode(QStringView{}, m_engine, 0, html, css, js, cssDoneIds, jsDoneIds);

    setWindowTitle(tr("Preview — %1 [%2]").arg(rec->permalink, rec->lang));
    ui->textBrowser->setHtml(html);
}
