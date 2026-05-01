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
    ui->splitter->setSizes({150, 950});

    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(ui->listLanguages, &QListWidget::currentRowChanged,
            this, &DialogPreviewPage::_onLanguageSelected);

    // -------------------------------------------------------------------------
    // Resolve root page.
    // -------------------------------------------------------------------------
    const auto &startRec = repo.findById(pageId);
    if (!startRec) {
        ui->textBrowser->setPlainText(tr("Page not found."));
        return;
    }
    const int rootId = (startRec->sourcePageId > 0) ? startRec->sourcePageId : pageId;

    const auto &rootRec = repo.findById(rootId);
    if (!rootRec) {
        ui->textBrowser->setPlainText(tr("Page not found."));
        return;
    }

    // -------------------------------------------------------------------------
    // 1. Source language — always the first entry.
    // -------------------------------------------------------------------------
    {
        PreviewEntry e;
        e.pageIdToLoad = rootId;
        e.lang         = rootRec->lang;
        m_entries.append(e);

        auto *item = new QListWidgetItem(rootRec->lang, ui->listLanguages);
        item->setToolTip(rootRec->permalink);
    }

    // Track which languages are already listed to avoid duplicates.
    QSet<QString> listedLangs;
    listedLangs.insert(rootRec->lang);

    // -------------------------------------------------------------------------
    // 2. Inline translations — scan source page data for "tr:<lang>:" keys.
    //    These are written by the new PageTranslator (stored in the source page).
    // -------------------------------------------------------------------------
    const QHash<QString, QString> &sourceData = repo.loadData(rootId);
    {
        // Keys look like "0_tr:fr:text" or "2_tr:de:title:hash".
        static const QRegularExpression reTrKey(QStringLiteral("_tr:([^:]+):"));
        QSet<QString> inlineLangs;
        for (auto it = sourceData.cbegin(); it != sourceData.cend(); ++it) {
            const auto m = reTrKey.match(it.key());
            if (m.hasMatch()) {
                inlineLangs.insert(m.captured(1));
            }
        }

        // Add each discovered inline language in engine order (stable sort).
        const int engineRows = engine.rowCount();
        for (int i = 0; i < engineRows; ++i) {
            const QString lang = engine.getLangCode(i);
            if (lang.isEmpty() || listedLangs.contains(lang) || !inlineLangs.contains(lang)) {
                continue;
            }
            listedLangs.insert(lang);

            PreviewEntry e;
            e.pageIdToLoad = rootId;   // load from source page
            e.lang         = lang;
            m_entries.append(e);

            auto *item = new QListWidgetItem(lang, ui->listLanguages);
            item->setToolTip(rootRec->permalink);
        }
    }

    // -------------------------------------------------------------------------
    // 3. Legacy translation page records (pre-inline system, sourcePageId != 0).
    // -------------------------------------------------------------------------
    const QList<PageRecord> &translations = repo.findTranslations(rootId);
    for (const PageRecord &t : std::as_const(translations)) {
        if (listedLangs.contains(t.lang)) {
            continue; // already covered by inline entry
        }
        listedLangs.insert(t.lang);

        PreviewEntry e;
        e.pageIdToLoad = t.id;
        e.lang         = t.lang;
        m_entries.append(e);

        auto *item = new QListWidgetItem(t.lang, ui->listLanguages);
        item->setToolTip(t.permalink);
        if (t.translatedAt.isEmpty()) {
            item->setText(t.lang + QStringLiteral(" [pending]"));
        }
    }

    // -------------------------------------------------------------------------
    // Select the row matching the originally requested pageId.
    // -------------------------------------------------------------------------
    int selectedRow = 0;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).pageIdToLoad == pageId
            && m_entries.at(i).lang == startRec->lang) {
            selectedRow = i;
            break;
        }
    }
    ui->listLanguages->setCurrentRow(selectedRow);
}

DialogPreviewPage::~DialogPreviewPage()
{
    delete ui;
}

void DialogPreviewPage::_onLanguageSelected(int row)
{
    if (row < 0 || row >= m_entries.size()) {
        return;
    }
    _renderPage(m_entries.at(row));
}

int DialogPreviewPage::_engineIndexForLang(const QString &lang) const
{
    for (int i = 0; i < m_engine.rowCount(); ++i) {
        if (m_engine.getLangCode(i) == lang) {
            return i;
        }
    }
    return 0;
}

void DialogPreviewPage::_renderPage(const PreviewEntry &entry)
{
    const auto &rec = m_repo.findById(entry.pageIdToLoad);
    if (!rec) {
        ui->textBrowser->setPlainText(tr("Page not found."));
        return;
    }

    auto type = AbstractPageType::createForTypeId(rec->typeId, m_categoryTable);
    if (!type) {
        ui->textBrowser->setPlainText(tr("Unknown page type: %1").arg(rec->typeId));
        return;
    }

    const QHash<QString, QString> &data = m_repo.loadData(entry.pageIdToLoad);
    type->load(data);

    // For inline translations, the source lang must be set so the page type
    // knows which fields are already in the source language vs translated.
    type->setAuthorLang(rec->lang);

    // Resolve the engine row for the requested language so addCode picks the
    // correct tr:<lang>:* translation keys during rendering.
    const int engineIdx = _engineIndexForLang(entry.lang);

    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    type->addCode(QStringView{}, m_engine, engineIdx, html, css, js, cssDoneIds, jsDoneIds);

    _inlineSvgs(html);

    setWindowTitle(tr("Preview — %1 [%2]").arg(rec->permalink, entry.lang));
    ui->textBrowser->setHtml(html);
}

void DialogPreviewPage::_inlineSvgs(QString &html)
{
    const QString dbPath = m_workingDir.filePath(QStringLiteral("images.db"));
    if (!QFile::exists(dbPath)) {
        return;
    }

    static const QRegularExpression reImg(
        QStringLiteral("<img\\b[^>]+src=\"/([^\"]+\\.svg)\"[^>]*>"),
        QRegularExpression::CaseInsensitiveOption);

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
