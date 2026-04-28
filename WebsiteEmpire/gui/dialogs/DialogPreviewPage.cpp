#include "DialogPreviewPage.h"
#include "ui_DialogPreviewPage.h"

#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/attributes/CategoryTable.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QListWidgetItem>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSvgRenderer>

DialogPreviewPage::DialogPreviewPage(IPageRepository &repo,
                                     CategoryTable   &categoryTable,
                                     AbstractEngine  &engine,
                                     const QDir      &workingDir,
                                     int              pageId,
                                     QWidget         *parent)
    : QDialog(parent)
    , ui(new Ui::DialogPreviewPage)
    , m_repo(repo)
    , m_categoryTable(categoryTable)
    , m_engine(engine)
    , m_workingDir(workingDir)
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

    _inlineSvgs(html);

    setWindowTitle(tr("Preview — %1 [%2]").arg(rec->permalink, rec->lang));
    ui->textBrowser->setHtml(html);
}

void DialogPreviewPage::_inlineSvgs(QString &html)
{
    const QString dbPath = m_workingDir.filePath(QStringLiteral("images.db"));
    if (!QFile::exists(dbPath)) {
        return;
    }

    // Match <img ... src="/file.svg" ...> — capture the bare filename (no slash).
    static const QRegularExpression reImg(
        QStringLiteral("<img\\b[^>]+src=\"/([^\"]+\\.svg)\"[^>]*>"),
        QRegularExpression::CaseInsensitiveOption);

    // Collect unique filenames present in this HTML.
    QStringList filenames;
    {
        auto it = reImg.globalMatch(html);
        while (it.hasNext()) {
            const QString fn = it.next().captured(1);
            if (!filenames.contains(fn)) {
                filenames.append(fn);
            }
        }
    }
    if (filenames.isEmpty()) {
        return;
    }

    // Load blobs from images.db — one connection, scoped tightly.
    QHash<QString, QByteArray> blobs;
    const QString connName = QStringLiteral("preview_svg_")
                           + QString::number(reinterpret_cast<quintptr>(this));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            for (const QString &fn : std::as_const(filenames)) {
                QSqlQuery q(db);
                q.prepare(QStringLiteral(
                    "SELECT b.blob FROM images b "
                    "JOIN image_names n ON n.image_id = b.id "
                    "WHERE n.filename = :fn LIMIT 1"));
                q.bindValue(QStringLiteral(":fn"), fn);
                if (q.exec() && q.next()) {
                    blobs.insert(fn, q.value(0).toByteArray());
                }
            }
        }
    }
    QSqlDatabase::removeDatabase(connName);

    if (blobs.isEmpty()) {
        return;
    }

    // Replace each matched <img> with a PNG data URI rendered from the SVG blob.
    QString result;
    result.reserve(html.size());
    int pos = 0;
    auto it = reImg.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        result += html.mid(pos, m.capturedStart() - pos);
        pos = m.capturedEnd();

        const QString &fn = m.captured(1);
        const auto blobIt = blobs.constFind(fn);
        if (blobIt == blobs.cend()) {
            result += m.captured(0);
            continue;
        }

        QSvgRenderer renderer(blobIt.value());
        if (!renderer.isValid()) {
            result += m.captured(0);
            continue;
        }

        QSize size = renderer.defaultSize();
        if (!size.isValid() || size.isEmpty()) {
            size = QSize(800, 400);
        } else {
            size = size.scaled(800, 600, Qt::KeepAspectRatio);
        }

        QImage img(size, QImage::Format_ARGB32);
        img.fill(Qt::white);
        QPainter painter(&img);
        renderer.render(&painter);
        painter.end();

        QBuffer buf;
        buf.open(QBuffer::WriteOnly);
        img.save(&buf, "PNG");

        const QString dataUri = QStringLiteral("data:image/png;base64,")
                              + QString::fromLatin1(buf.data().toBase64());

        QString tag = m.captured(0);
        tag.replace(QStringLiteral("src=\"/") + fn + QStringLiteral("\""),
                    QStringLiteral("src=\"") + dataUri + QStringLiteral("\""));
        result += tag;
    }
    result += html.mid(pos);
    html = result;
}
