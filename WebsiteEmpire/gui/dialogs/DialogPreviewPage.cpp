#include "DialogPreviewPage.h"
#include "ui_DialogPreviewPage.h"

#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/attributes/CategoryTable.h"

#include "ExceptionWithTitleText.h"

#include <QBuffer>
#include <QDesktopServices>
#include <QFile>
#include <QImage>
#include <QListWidgetItem>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSvgRenderer>
#include <QTextStream>
#include <QUrl>

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

    connect(ui->buttonBox,    &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(ui->btnOpenBrowser, &QPushButton::clicked, this, &DialogPreviewPage::_onOpenInBrowser);
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
    // 4. Engine languages not yet listed.
    //    Some page types (e.g. category hub) render in any language via
    //    addCode() without storing per-language data in the page record.
    //    Adding remaining engine languages lets those pages be previewed in
    //    every configured language.
    // -------------------------------------------------------------------------
    for (int i = 0; i < engine.rowCount(); ++i) {
        const QString lang = engine.getLangCode(i);
        if (lang.isEmpty() || listedLangs.contains(lang)) {
            continue;
        }
        listedLangs.insert(lang);

        PreviewEntry e;
        e.pageIdToLoad = rootId;
        e.lang         = lang;
        m_entries.append(e);

        auto *item = new QListWidgetItem(lang, ui->listLanguages);
        item->setToolTip(rootRec->permalink);
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
    type->bindGenerationContext(m_repo, m_workingDir);

    // Resolve the engine row for the requested language so addCode picks the
    // correct tr:<lang>:* translation keys during rendering.
    const int engineIdx = _engineIndexForLang(entry.lang);

    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    try {
        type->addCode(QStringView{}, m_engine, engineIdx, html, css, js, cssDoneIds, jsDoneIds);
    } catch (const ExceptionWithTitleText &ex) {
        ui->textBrowser->setPlainText(ex.errorTitle() + QStringLiteral("\n\n") + ex.errorText());
        ui->btnOpenBrowser->setEnabled(false);
        return;
    }

    _inlineSvgs(html, entry.lang);
    const QString domain = m_engine.data(
        m_engine.index(engineIdx, AbstractEngine::COL_DOMAIN)).toString();
    _inlineRasterImages(html, domain);

    QString fullHtml;
    fullHtml.reserve(css.size() + html.size() + 64);
    if (!css.isEmpty()) {
        fullHtml += QStringLiteral("<style>");
        fullHtml += css;
        fullHtml += QStringLiteral("</style>");
    }
    fullHtml += html;

    setWindowTitle(tr("Preview — %1 [%2]").arg(rec->permalink, entry.lang));
    ui->textBrowser->setHtml(fullHtml);

    // Write to temp file so "Open in browser" can show accurate CSS rendering.
    m_tempHtmlPath = QDir::tempPath()
                   + QStringLiteral("/website_empire_preview_")
                   + entry.lang
                   + QStringLiteral(".html");
    QFile f(m_tempHtmlPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        out << fullHtml;
    } else {
        m_tempHtmlPath.clear();
    }
    ui->btnOpenBrowser->setEnabled(!m_tempHtmlPath.isEmpty());
}

void DialogPreviewPage::_onOpenInBrowser()
{
    if (!m_tempHtmlPath.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_tempHtmlPath));
    }
}

void DialogPreviewPage::_inlineSvgs(QString &html, const QString &lang)
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
                // Prefer the translated blob (domain=lang); fall back to source (domain='').
                q.prepare(QStringLiteral(
                    "SELECT b.blob FROM images b"
                    " JOIN image_names n ON n.image_id = b.id"
                    " WHERE n.filename = :fn AND (n.domain = :lang OR n.domain = '')"
                    " ORDER BY CASE WHEN n.domain = :lang THEN 0 ELSE 1 END"
                    " LIMIT 1"));
                q.bindValue(QStringLiteral(":fn"),   fn);
                q.bindValue(QStringLiteral(":lang"), lang);
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

void DialogPreviewPage::_inlineRasterImages(QString &html, const QString &domain)
{
    const QString dbPath = m_workingDir.filePath(QStringLiteral("images.db"));
    if (!QFile::exists(dbPath)) {
        return;
    }

    // Match any src="/images/..." attribute.  The domain segment may be empty
    // (producing a double slash like /images//file.webp) when no domain is
    // configured in the engine, so we extract the filename from the last path
    // component rather than trying to parse out a specific domain slot.
    static const QRegularExpression reSrc(
        QStringLiteral("src=\"(/images/[^\"]*)\""),
        QRegularExpression::CaseInsensitiveOption);

    QStringList filenames;
    {
        auto it = reSrc.globalMatch(html);
        while (it.hasNext()) {
            const QString path = it.next().captured(1);
            const int lastSlash = path.lastIndexOf(QLatin1Char('/'));
            const QString fn = (lastSlash >= 0) ? path.mid(lastSlash + 1) : path;
            if (!fn.isEmpty() && !filenames.contains(fn)) {
                filenames.append(fn);
            }
        }
    }
    if (filenames.isEmpty()) {
        return;
    }

    // Load blobs: prefer exact domain, fall back to domain="".
    QHash<QString, QPair<QByteArray, QString>> blobs; // filename → (blob, mimeType)
    const QString connName = QStringLiteral("preview_raster_")
                           + QString::number(reinterpret_cast<quintptr>(this));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            for (const QString &fn : std::as_const(filenames)) {
                QSqlQuery q(db);
                q.prepare(QStringLiteral(
                    "SELECT i.blob, i.mime_type FROM images i"
                    " JOIN image_names n ON n.image_id = i.id"
                    " WHERE n.filename = :fn AND (n.domain = :domain OR n.domain = '')"
                    " ORDER BY CASE WHEN n.domain = :domain THEN 0 ELSE 1 END"
                    " LIMIT 1"));
                q.bindValue(QStringLiteral(":fn"),     fn);
                q.bindValue(QStringLiteral(":domain"), domain);
                if (q.exec() && q.next()) {
                    blobs.insert(fn, {q.value(0).toByteArray(),
                                      q.value(1).toString()});
                }
            }
        }
    }
    QSqlDatabase::removeDatabase(connName);

    if (blobs.isEmpty()) {
        return;
    }

    // Replace each src="/images/..." with the corresponding data URI.
    QString result;
    result.reserve(html.size());
    int pos = 0;
    auto it = reSrc.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        result += html.mid(pos, m.capturedStart() - pos);
        pos = m.capturedEnd();

        const QString path = m.captured(1);
        const int lastSlash = path.lastIndexOf(QLatin1Char('/'));
        const QString fn = (lastSlash >= 0) ? path.mid(lastSlash + 1) : path;

        const auto blobIt = blobs.constFind(fn);
        if (blobIt == blobs.cend()) {
            result += m.captured(0);
            continue;
        }

        const QString dataUri = QStringLiteral("data:")
                              + blobIt->second
                              + QStringLiteral(";base64,")
                              + QString::fromLatin1(blobIt->first.toBase64());

        result += QStringLiteral("src=\"");
        result += dataUri;
        result += QStringLiteral("\"");
    }
    result += html.mid(pos);
    html = result;
}
