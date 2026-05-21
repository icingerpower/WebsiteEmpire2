#include "PageTranslator.h"

#include "website/AbstractEngine.h"
#include "website/pages/AbstractLegalPageDef.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageGenerationState.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageTypeLegal.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/social/AbstractSocialMedia.h"
#include "website/translation/TranslationProtocol.h"

#include <algorithm>

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QTextStream>

// =============================================================================
// Constructor / Destructor
// =============================================================================

PageTranslator::PageTranslator(IPageRepository &repo,
                                CategoryTable   &categoryTable,
                                const QDir      &workingDir,
                                QObject         *parent)
    : QObject(parent)
    , m_repo(repo)
    , m_categoryTable(categoryTable)
    , m_workingDir(workingDir)
{
}

PageTranslator::~PageTranslator()
{
    if (m_logFile && m_logFile->isOpen()) {
        m_logFile->close();
    }
    delete m_logFile;
}

// =============================================================================
// start
// =============================================================================

void PageTranslator::start(AbstractEngine *engine, const QString &editingLang)
{
    _openLogFile();

    if (!engine) {
        _log(QStringLiteral("ERROR: No engine provided."), true);
        _emitFinished(0, 1);
        return;
    }
    if (editingLang.isEmpty()) {
        _log(QStringLiteral("ERROR: Editing language is not configured."), true);
        _emitFinished(0, 1);
        return;
    }

    m_queue = _buildJobQueue(engine, editingLang);
    _log(QStringLiteral("Translation started. %1 job(s) queued.").arg(m_queue.size()));

    if (m_queue.isEmpty()) {
        _log(QStringLiteral("Nothing to translate. All pages are up to date."));
        _emitFinished(0, 0);
        return;
    }

    _processNextJob();
}

// =============================================================================
// startWithJobs
// =============================================================================

void PageTranslator::startWithJobs(const QList<TranslationJob> &jobs)
{
    _openLogFile();

    m_queue = jobs;
    _log(QStringLiteral("Translation started. %1 job(s) queued.").arg(m_queue.size()));

    if (m_queue.isEmpty()) {
        _log(QStringLiteral("Nothing to translate. All pages are up to date."));
        _emitFinished(0, 0);
        return;
    }

    _processNextJob();
}

// =============================================================================
// startSvgJobs
// =============================================================================

void PageTranslator::startSvgJobs(const QString &editingLang,
                                   const QString &languageFilter,
                                   int            limit)
{
    _openLogFile();

    if (editingLang.isEmpty()) {
        _log(QStringLiteral("ERROR: Editing language is not configured."), true);
        _emitFinished(0, 1);
        return;
    }

    // Load two sets from images.db in one pass:
    //   translatedPairs  — (lang, filename) pairs that are already translated
    //   sourceSvgNames   — SVG filenames that have a source blob (domain='')
    // Only SVGs present in sourceSvgNames can be translated; the rest are
    // silently skipped rather than counted as errors.
    QSet<QString> translatedPairs; // encoded as "lang\nfilename"
    QSet<QString> sourceSvgNames;
    const QString dbPath = m_workingDir.filePath(QStringLiteral("images.db"));
    if (QFile::exists(dbPath)) {
        const QString connName = QStringLiteral("svgjobs_check_%1")
                                     .arg(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(dbPath);
            if (db.open()) {
                QSqlQuery q(db);
                if (q.exec(QStringLiteral(
                        "SELECT domain, filename FROM image_names"))) {
                    while (q.next()) {
                        const QString domain   = q.value(0).toString();
                        const QString filename = q.value(1).toString();
                        if (domain.isEmpty()) {
                            sourceSvgNames.insert(filename);
                        } else {
                            translatedPairs.insert(domain + QLatin1Char('\n') + filename);
                        }
                    }
                }
            }
        }
        QSqlDatabase::removeDatabase(connName);
    }

    static const QRegularExpression reImgFix(
        QStringLiteral("\\[IMGFIX\\b[^\\]]*fileName=\"([^\"]+\\.svg)\""),
        QRegularExpression::CaseInsensitiveOption);

    const QList<PageRecord> &sources = m_repo.findSourcePages(editingLang);
    _log(QStringLiteral("SVG jobs: scanning %1 source page(s)%2…")
             .arg(sources.size())
             .arg(languageFilter.isEmpty()
                 ? QString{}
                 : QStringLiteral(" (language filter: %1)").arg(languageFilter)));

    for (const PageRecord &src : std::as_const(sources)) {
        if (src.langCodesToTranslate.isEmpty()) {
            continue;
        }
        const QHash<QString, QString> &data = m_repo.loadData(src.id);
        const QString &text1 = data.value(QStringLiteral("1_text"));
        if (text1.isEmpty()) {
            continue;
        }

        QStringList svgFns;
        {
            auto it = reImgFix.globalMatch(text1);
            while (it.hasNext()) {
                const QString fn = it.next().captured(1);
                if (!svgFns.contains(fn)) {
                    svgFns.append(fn);
                }
            }
        }
        if (svgFns.isEmpty()) {
            continue;
        }

        for (const QString &fn : std::as_const(svgFns)) {
            for (const QString &targetLang : std::as_const(src.langCodesToTranslate)) {
                if (!languageFilter.isEmpty() && targetLang != languageFilter) {
                    continue;
                }
                if (translatedPairs.contains(targetLang + QLatin1Char('\n') + fn)) {
                    continue;
                }
                if (!sourceSvgNames.contains(fn)) {
                    continue; // source blob not yet in images.db — skip silently
                }
                TranslationJob job;
                job.pageId      = src.id;
                job.typeId      = src.typeId;
                job.sourceLang  = editingLang;
                job.targetLang  = targetLang;
                job.svgFilename = fn;
                m_queue.append(job);
                _log(QStringLiteral("  Queued: %1  SVG: %2  → %3")
                         .arg(src.permalink, fn, targetLang));
            }
        }
    }

    if (limit >= 1 && m_queue.size() > limit) {
        m_queue.resize(limit);
        _log(QStringLiteral("SVG jobs limited to %1.").arg(limit));
    }

    _log(QStringLiteral("SVG jobs queued: %1").arg(m_queue.size()));

    if (m_queue.isEmpty()) {
        _log(QStringLiteral("All SVG images are already translated."));
        _emitFinished(0, 0);
        return;
    }

    _processNextJob();
}

// =============================================================================
// buildPrompts (view without executing)
// =============================================================================

QString PageTranslator::buildPrompts(AbstractEngine *engine, const QString &editingLang)
{
    if (!engine || editingLang.isEmpty()) {
        return tr("No engine or editing language configured.");
    }

    const QList<TranslationJob> jobs = _buildJobQueue(engine, editingLang);
    if (jobs.isEmpty()) {
        return tr("No pending translations. All pages are up to date.");
    }

    QString out;
    QTextStream ts(&out);
    ts << tr("Pending translations: %1\n").arg(jobs.size());

    int n = 1;
    for (const TranslationJob &job : std::as_const(jobs)) {
        const auto &srcRec = m_repo.findById(job.pageId);
        const QString srcTitle = srcRec ? srcRec->permalink : QStringLiteral("?");

        ts << QStringLiteral("--- Job %1: \"%2\"  (%3 → %4) ---\n")
                  .arg(n++)
                  .arg(srcTitle, job.sourceLang, job.targetLang);

        auto type = AbstractPageType::createForTypeId(job.typeId, m_categoryTable);
        if (!type) {
            ts << tr("  (unknown page type '%1')\n\n").arg(job.typeId);
            continue;
        }

        const QHash<QString, QString> &data = m_repo.loadData(job.pageId);
        if (data.isEmpty()) {
            ts << tr("  (source page has no data)\n\n");
            continue;
        }

        type->load(data);
        type->setAuthorLang(job.sourceLang);

        QList<TranslatableField> fields;
        type->collectTranslatables(QStringView{}, fields);

        if (fields.isEmpty()) {
            ts << tr("  (no translatable fields)\n\n");
            continue;
        }

        ts << QStringLiteral("User message to send to Claude:\n\n");
        ts << TranslationProtocol::buildPrompt(fields, job.sourceLang, job.targetLang);
        ts << QStringLiteral("\n\n");
    }

    return out;
}

// =============================================================================
// Private: _buildJobQueue
// =============================================================================

QList<PageTranslator::TranslationJob>
PageTranslator::_buildJobQueue(AbstractEngine *engine, const QString &editingLang)
{
    QList<TranslationJob> queue;

    const QList<PageRecord> &sources = m_repo.findSourcePages(editingLang);
    _log(QStringLiteral("Source pages found: %1").arg(sources.size()));

    for (const PageRecord &src : std::as_const(sources)) {
        if (src.langCodesToTranslate.isEmpty()) {
            _log(QStringLiteral("  Page %1 (%2): no target languages set — skipping")
                     .arg(src.id).arg(src.permalink));
            continue;
        }

        for (const QString &targetLang : std::as_const(src.langCodesToTranslate)) {
            TranslationJob job;
            job.pageId     = src.id;
            job.typeId     = src.typeId;
            job.sourceLang = editingLang;
            job.targetLang = targetLang;
            queue.append(job);

            _log(QStringLiteral("  Page %1 (%2): queued for %3")
                     .arg(src.id).arg(src.permalink, targetLang));
        }
    }

    std::stable_sort(queue.begin(), queue.end(),
                     [](const TranslationJob &a, const TranslationJob &b) {
                         const bool aLegal = (a.typeId == QLatin1String(PageTypeLegal::TYPE_ID));
                         const bool bLegal = (b.typeId == QLatin1String(PageTypeLegal::TYPE_ID));
                         return aLegal && !bLegal;
                     });

    Q_UNUSED(engine)
    return queue;
}

// =============================================================================
// Private: _processNextJob
// =============================================================================

void PageTranslator::_processNextJob()
{
    if (m_queue.isEmpty()) {
        _log(QStringLiteral("All jobs done. Translated: %1  Errors: %2")
                 .arg(m_translated).arg(m_errors));
        _emitFinished(m_translated, m_errors);
        return;
    }

    m_currentJob = m_queue.takeFirst();

    // -------------------------------------------------------------------------
    // SVG translation sub-job
    // -------------------------------------------------------------------------
    if (!m_currentJob.svgFilename.isEmpty()) {
        const auto &svgPageRec = m_repo.findById(m_currentJob.pageId);
        const QString svgPermalink = svgPageRec ? svgPageRec->permalink : QStringLiteral("?");
        _log(QStringLiteral("Processing SVG %1 → %2  (%3)")
                 .arg(m_currentJob.svgFilename, m_currentJob.targetLang, svgPermalink));

        const QString dbPath = m_workingDir.filePath(QStringLiteral("images.db"));
        bool alreadyTranslated = false;
        QByteArray sourceBlob;

        const QString connName = QStringLiteral("transl_svg_chk_%1")
                                     .arg(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(dbPath);
            if (db.open()) {
                QSqlQuery qCheck(db);
                qCheck.prepare(QStringLiteral(
                    "SELECT COUNT(*) FROM image_names"
                    " WHERE filename = :fn AND domain = :lang"));
                qCheck.bindValue(QStringLiteral(":fn"),   m_currentJob.svgFilename);
                qCheck.bindValue(QStringLiteral(":lang"), m_currentJob.targetLang);
                if (qCheck.exec() && qCheck.next() && qCheck.value(0).toInt() > 0) {
                    alreadyTranslated = true;
                }
                if (!alreadyTranslated) {
                    QSqlQuery qBlob(db);
                    qBlob.prepare(QStringLiteral(
                        "SELECT b.blob FROM images b"
                        " JOIN image_names n ON n.image_id = b.id"
                        " WHERE n.filename = :fn AND n.domain = '' LIMIT 1"));
                    qBlob.bindValue(QStringLiteral(":fn"), m_currentJob.svgFilename);
                    if (qBlob.exec() && qBlob.next()) {
                        sourceBlob = qBlob.value(0).toByteArray();
                    }
                }
            }
        }
        QSqlDatabase::removeDatabase(connName);

        if (alreadyTranslated) {
            _log(QStringLiteral("  SVG %1 already translated into %2 — skipping")
                     .arg(m_currentJob.svgFilename, m_currentJob.targetLang));
            _processNextJob();
            return;
        }
        if (sourceBlob.isEmpty()) {
            _log(QStringLiteral("  SVG %1: no source blob in images.db — skipping")
                     .arg(m_currentJob.svgFilename));
            _processNextJob();
            return;
        }

        const QString context = svgPermalink == QStringLiteral("?") ? QString{} : svgPermalink;

        const QString prompt =
            QStringLiteral("Translate all human-readable text in this SVG image from ")
            + m_currentJob.sourceLang
            + QStringLiteral(" to ")
            + m_currentJob.targetLang
            + QStringLiteral(
                  ". Preserve the exact SVG structure — only modify text inside <text>,"
                  " <tspan>, and similar text elements. Do not translate variable names,"
                  " scientific symbols, abbreviations, or numeric values."
                  " Reply with ONLY the complete translated SVG starting with <svg"
                  " and ending with </svg>. No explanation.\n\n")
            + (context.isEmpty()
                ? QString{}
                : QStringLiteral("Article: ") + context + QStringLiteral("\n\n"))
            + QString::fromUtf8(sourceBlob);

        m_tempDir = std::make_unique<QTemporaryDir>();
        if (!m_tempDir->isValid()) {
            _log(QStringLiteral("  Failed to create temp dir for SVG %1")
                     .arg(m_currentJob.svgFilename), true);
            ++m_errors;
            m_tempDir.reset();
            _processNextJob();
            return;
        }

        const QString promptPath = m_tempDir->path() + QStringLiteral("/prompt.txt");
        {
            QFile f(promptPath);
            if (!f.open(QIODevice::WriteOnly)) {
                _log(QStringLiteral("  Failed to write prompt file for SVG %1")
                         .arg(m_currentJob.svgFilename), true);
                ++m_errors;
                m_tempDir.reset();
                _processNextJob();
                return;
            }
            f.write(prompt.toUtf8());
        }

        m_processOutput.clear();

        m_process = new QProcess(this);
        m_process->setProgram(QStringLiteral("claude"));
        m_process->setArguments({QStringLiteral("-p"), QStringLiteral("-"),
                                  QStringLiteral("--dangerously-skip-permissions"),
                                  QStringLiteral("--tools"), QStringLiteral(""),
                                  QStringLiteral("--output-format"), QStringLiteral("stream-json"),
                                  QStringLiteral("--verbose")});
        m_process->setWorkingDirectory(m_tempDir->path());
        m_process->setStandardInputFile(promptPath);

        connect(m_process, &QProcess::readyReadStandardOutput,
                this, &PageTranslator::_onProcessReadyRead);
        connect(m_process,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &PageTranslator::_onProcessFinished);

        m_process->start();
        return;
    }

    // -------------------------------------------------------------------------
    // Text translation job
    // -------------------------------------------------------------------------

    // Load source page record — needed for endPermalink slug translation.
    const auto srcRec = m_repo.findById(m_currentJob.pageId);
    {
        const QString permalink = srcRec ? srcRec->permalink : QStringLiteral("?");
        _log(QStringLiteral("Processing page %1 → %2  (%3)  job %4")
                 .arg(m_currentJob.targetLang, permalink)
                 .arg(m_currentJob.pageId)
                 .arg(m_translated + m_errors + 1));
    }

    m_currentPageType = AbstractPageType::createForTypeId(m_currentJob.typeId, m_categoryTable);
    if (!m_currentPageType) {
        _log(QStringLiteral("  Skipping: unknown page type '%1'.")
                 .arg(m_currentJob.typeId));
        _processNextJob();
        return;
    }

    m_currentPageData = m_repo.loadData(m_currentJob.pageId);
    if (m_currentPageData.isEmpty()) {
        _log(QStringLiteral("  Skipping: page %1 has no data.")
                 .arg(m_currentJob.pageId));
        _processNextJob();
        return;
    }

    m_currentPageType->load(m_currentPageData);
    m_currentPageType->setAuthorLang(m_currentJob.sourceLang);

    if (m_currentPageType->isTranslationComplete(QStringView{}, m_currentJob.targetLang)) {
        _log(QStringLiteral("  Page %1 already fully translated into %2 — skipping")
                 .arg(m_currentJob.pageId).arg(m_currentJob.targetLang));
        _processNextJob();
        return;
    }

    QList<TranslatableField> fields;
    m_currentPageType->collectTranslatables(QStringView{}, fields);

    // If this page has an endPermalink suffix AND no translated slug yet, add the
    // full URL slug as a translatable field so the generator can use a language-
    // specific path (e.g. /fr/douleurs-genoux-genes-biomarqueurs).
    const QString trSlugKey = QStringLiteral("tr:")
                              + m_currentJob.targetLang
                              + QStringLiteral(":_permalink_slug");
    if (srcRec && !srcRec->endPermalink.isEmpty()
            && !m_currentPageData.contains(trSlugKey)) {
        // Derive the slug from the stored permalink (strip leading "/" only).
        QString sourceSlug = srcRec->permalink;
        if (sourceSlug.startsWith(QLatin1Char('/'))) {
            sourceSlug.remove(0, 1);
        }
        TranslatableField slugField;
        slugField.id = QStringLiteral("_permalink_slug");
        slugField.sourceText =
            QStringLiteral("[This field is a URL slug. "
                           "Output a valid URL slug only: "
                           "lowercase words separated by hyphens, "
                           "no spaces, no special characters.]\n")
            + sourceSlug;
        fields.append(slugField);
    }

    if (fields.isEmpty()) {
        _log(QStringLiteral("  Page %1 has no translatable content — skipping")
                 .arg(m_currentJob.pageId));
        _processNextJob();
        return;
    }

    // -------------------------------------------------------------------------
    // Detect whether any field is too large to translate in a single call.
    // If so, split it into MAX_CHUNK_CHARS-sized pieces and translate each
    // piece with a separate claude call, reassembling the results at the end.
    // -------------------------------------------------------------------------
    m_chunkState.reset();
    for (const TranslatableField &f : std::as_const(fields)) {
        if (f.id == QStringLiteral("_permalink_slug")) {
            continue; // slug is always tiny — never needs chunking
        }
        if (f.sourceText.size() > MAX_CHUNK_CHARS) {
            const QStringList chunks = _splitAtBlocks(f.sourceText, MAX_CHUNK_CHARS);
            if (chunks.size() > 1) {
                ChunkState cs;
                cs.fieldId      = f.id;
                cs.totalChunks  = chunks.size();
                cs.chunkIndex   = 0;
                cs.pendingChunks = chunks;  // all chunks; first taken below
                m_chunkState = std::move(cs);
                break; // only the first oversized field triggers chunking
            }
        }
    }

    if (m_chunkState.has_value()) {
        auto &cs = m_chunkState.value();
        _log(QStringLiteral("  Page %1: field '%2' is large — splitting into %3 chunk(s)")
                 .arg(m_currentJob.pageId).arg(cs.fieldId).arg(cs.totalChunks));

        // Build the first call: non-chunk fields (e.g. slug) + first chunk.
        QList<TranslatableField> firstBatch;
        for (const TranslatableField &f : std::as_const(fields)) {
            if (f.id != cs.fieldId) {
                firstBatch.append(f);
            }
        }
        TranslatableField chunkField;
        chunkField.id         = cs.fieldId;
        chunkField.sourceText = cs.pendingChunks.takeFirst();
        firstBatch.append(chunkField);
        cs.chunkIndex = 1;

        _launchTextTranslation(firstBatch);
    } else {
        _launchTextTranslation(fields);
    }
}

// =============================================================================
// Private: _onProcessFinished
// =============================================================================

void PageTranslator::_onProcessFinished(int exitCode, QProcess::ExitStatus /*status*/)
{
    QString translatedJson;
    bool hasError = false;

    if (m_process->error() == QProcess::FailedToStart) {
        _log(QStringLiteral("  claude executable not found in PATH for page %1 → %2")
                 .arg(m_currentJob.pageId).arg(m_currentJob.targetLang), true);
        hasError = true;
    } else if (exitCode != 0) {
        // Drain remaining stdout — Claude may have produced a valid response before
        // the error (e.g. hitting max output tokens while still generating).
        m_processOutput += m_process->readAllStandardOutput();

        const QString err = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
        _log(QStringLiteral("  claude error for page %1 → %2: %3  (stdout: %4 bytes)")
                 .arg(m_currentJob.pageId).arg(m_currentJob.targetLang,
                      err.isEmpty() ? QStringLiteral("exit code %1").arg(exitCode) : err)
                 .arg(m_processOutput.size()), true);

        // Save raw stdout so the cause can be diagnosed offline.
        {
            const QString rawPath = m_workingDir.filePath(
                QStringLiteral("translation_error_p%1_%2.bin")
                    .arg(m_currentJob.pageId).arg(m_currentJob.targetLang));
            QFile rf(rawPath);
            if (rf.open(QIODevice::WriteOnly)) {
                rf.write(m_processOutput);
                _log(QStringLiteral("  Stdout dump saved to: %1").arg(rawPath));
            }
        }

        hasError = true;
    } else {
        // Drain any remaining bytes not yet delivered via readyReadStandardOutput.
        m_processOutput += m_process->readAllStandardOutput();

        // Save raw stream-json for one-time diagnosis of truncation issues.
        {
            const QString rawPath = m_workingDir.filePath(
                QStringLiteral("translation_rawoutput_p%1_%2.bin")
                    .arg(m_currentJob.pageId).arg(m_currentJob.targetLang));
            QFile rf(rawPath);
            if (rf.open(QIODevice::WriteOnly)) {
                rf.write(m_processOutput);
                _log(QStringLiteral("  Raw process output saved to: %1 (%2 bytes)")
                         .arg(rawPath).arg(m_processOutput.size()));
            }
        }

        // The output is stream-json (one JSON object per line).
        // We collect text from two sources and use whichever is longer:
        //   1. {"type":"result","result":"..."} — final compiled response (may be tail-truncated for large outputs)
        //   2. {"type":"assistant","message":{"content":[{"type":"text","text":"..."}]}} — full assistant turn
        QString resultText;
        QString assistantText;

        for (const QByteArray &line : m_processOutput.split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }
            const QJsonDocument doc = QJsonDocument::fromJson(trimmed);
            if (!doc.isObject()) {
                continue;
            }
            const QJsonObject obj = doc.object();
            const QString type = obj.value(QStringLiteral("type")).toString();

            if (type == QStringLiteral("result")) {
                resultText = obj.value(QStringLiteral("result")).toString().trimmed();
                break; // result is always the last line — no need to scan further
            }

            if (type == QStringLiteral("assistant")) {
                const QJsonArray content = obj.value(QStringLiteral("message"))
                                               .toObject()
                                               .value(QStringLiteral("content"))
                                               .toArray();
                for (const QJsonValue &block : std::as_const(content)) {
                    const QJsonObject blk = block.toObject();
                    if (blk.value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
                        const QString t = blk.value(QStringLiteral("text")).toString().trimmed();
                        if (t.size() > assistantText.size()) {
                            assistantText = t;
                        }
                    }
                }
            }
        }

        _log(QStringLiteral("  result field: %1 chars  assistant content: %2 chars")
                 .arg(resultText.size()).arg(assistantText.size()));
        translatedJson = (assistantText.size() > resultText.size()) ? assistantText : resultText;
    }

    m_process->deleteLater();
    m_process = nullptr;
    m_tempDir.reset();

    if (hasError) {
        m_processOutput.clear();
        m_chunkState.reset();
        ++m_errors;
        _processNextJob();
        return;
    }

    // -------------------------------------------------------------------------
    // SVG job: validate and save the translated SVG blob.
    // -------------------------------------------------------------------------
    if (!m_currentJob.svgFilename.isEmpty()) {
        const QString svgTrimmed = translatedJson.trimmed();
        static const QRegularExpression reSvgStart(
            QStringLiteral("\\A\\s*<svg\\b"),
            QRegularExpression::CaseInsensitiveOption);
        if (svgTrimmed.isEmpty()
                || !reSvgStart.match(svgTrimmed).hasMatch()
                || !svgTrimmed.endsWith(QStringLiteral("</svg>"), Qt::CaseInsensitive)) {
            _log(QStringLiteral("  SVG translation for %1 → %2: invalid SVG response (starts: %3)")
                     .arg(m_currentJob.svgFilename, m_currentJob.targetLang,
                          svgTrimmed.left(80)),
                 true);
            m_processOutput.clear();
            ++m_errors;
            _processNextJob();
            return;
        }
        _saveSvgTranslation(m_currentJob.svgFilename, m_currentJob.targetLang,
                            svgTrimmed.toUtf8());

        // Rasterize each social-image WebP variant that matches this SVG stem.
        // The stem is the SVG filename without its "-og.svg"/"-wide.svg"/etc. suffix.
        // We also write WebP variants for all four canonical sizes.
        _rasterizeSvgToWebPs(m_currentJob.svgFilename,
                             svgTrimmed.toUtf8(),
                             m_currentJob.targetLang);

        // Mark translation image state as complete for this page × lang pair.
        m_repo.setTranslationImageState(m_currentJob.pageId,
                                        m_currentJob.targetLang,
                                        PageGenerationState::Complete);

        m_processOutput.clear();
        ++m_translated;
        _log(QStringLiteral("  SVG %1 → %2: done (%3 chars)")
                 .arg(m_currentJob.svgFilename, m_currentJob.targetLang)
                 .arg(svgTrimmed.size()));
        _processNextJob();
        return;
    }

    const QHash<QString, QString> translations = TranslationProtocol::parseResponse(translatedJson);

    if (translations.isEmpty()) {
        const QString preview = translatedJson.left(300).replace(QLatin1Char('\n'), QStringLiteral("↵"));
        _log(QStringLiteral("  Could not parse translation response for page %1 → %2. "
                            "Response length: %3. First 300 chars: [%4]")
                 .arg(m_currentJob.pageId).arg(m_currentJob.targetLang)
                 .arg(translatedJson.size()).arg(preview), true);

        // Save full raw response for diagnosis.
        const QString dumpPath = m_workingDir.filePath(
            QStringLiteral("translation_response_debug_p%1_%2.txt")
                .arg(m_currentJob.pageId).arg(m_currentJob.targetLang));
        {
            QFile f(dumpPath);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(translatedJson.toUtf8());
                _log(QStringLiteral("  Raw response saved to: %1").arg(dumpPath));
            }
        }

        m_processOutput.clear();
        m_chunkState.reset();
        ++m_errors;
        _processNextJob();
        return;
    }

    m_processOutput.clear();

    // -------------------------------------------------------------------------
    // Chunk mode: accumulate the result and dispatch the next chunk (or finalize).
    // -------------------------------------------------------------------------
    if (m_chunkState.has_value()) {
        auto &cs = m_chunkState.value();

        for (auto it = translations.cbegin(); it != translations.cend(); ++it) {
            if (it.key() == cs.fieldId) {
                if (!cs.assembledText.isEmpty()) {
                    cs.assembledText += QStringLiteral("\n\n");
                }
                cs.assembledText += it.value();
            } else {
                cs.otherTranslations.insert(it.key(), it.value());
            }
        }

        if (!cs.pendingChunks.isEmpty()) {
            _log(QStringLiteral("  Page %1 → %2: chunk %3/%4 done, continuing…")
                     .arg(m_currentJob.pageId).arg(m_currentJob.targetLang)
                     .arg(cs.chunkIndex).arg(cs.totalChunks));

            QList<TranslatableField> nextBatch;
            TranslatableField chunkField;
            chunkField.id         = cs.fieldId;
            chunkField.sourceText = cs.pendingChunks.takeFirst();
            nextBatch.append(chunkField);
            cs.chunkIndex++;

            _launchTextTranslation(nextBatch);
            return;
        }

        // All chunks done — assemble final translations and save.
        QHash<QString, QString> finalTranslations = cs.otherTranslations;
        finalTranslations.insert(cs.fieldId, cs.assembledText);
        m_chunkState.reset();

        _log(QStringLiteral("  Page %1 → %2: all chunks assembled")
                 .arg(m_currentJob.pageId).arg(m_currentJob.targetLang));
        _finalizeTextTranslations(finalTranslations);
        return;
    }

    _finalizeTextTranslations(translations);
}

void PageTranslator::_onProcessReadyRead()
{
    if (m_process) {
        m_processOutput += m_process->readAllStandardOutput();
    }
}

void PageTranslator::_emitFinished(int translated, int errors)
{
    QMetaObject::invokeMethod(this, [this, translated, errors]() {
        emit finished(translated, errors);
    }, Qt::QueuedConnection);
}

void PageTranslator::_log(const QString &msg, bool errorLevel)
{
    emit logMessage(msg);

    if (m_logFile && m_logFile->isOpen()) {
        QTextStream ts(m_logFile);
        ts << QDateTime::currentDateTimeUtc().toString(Qt::ISODate)
           << (errorLevel ? QStringLiteral(" [ERROR] ") : QStringLiteral(" [INFO]  "))
           << msg << QStringLiteral("\n");
        m_logFile->flush();
    }
}

// =============================================================================
// Private: _splitAtBlocks
// =============================================================================

QStringList PageTranslator::_splitAtBlocks(const QString &text, int maxChars)
{
    QStringList chunks;
    if (text.size() <= maxChars) {
        chunks.append(text);
        return chunks;
    }

    const QStringList paragraphs = text.split(QStringLiteral("\n\n"));
    QString current;

    for (const QString &para : std::as_const(paragraphs)) {
        const int candidate = current.size() + (current.isEmpty() ? 0 : 2) + para.size();
        if (!current.isEmpty() && candidate > maxChars) {
            // Only split here if shortcode brackets are balanced in current.
            const int opens  = current.count(QLatin1Char('['));
            const int closes = current.count(QLatin1Char(']'));
            if (opens <= closes) {
                chunks.append(current.trimmed());
                current = para;
                continue;
            }
        }
        if (!current.isEmpty()) {
            current += QStringLiteral("\n\n");
        }
        current += para;
    }
    if (!current.trimmed().isEmpty()) {
        chunks.append(current.trimmed());
    }
    return chunks.isEmpty() ? QStringList{text} : chunks;
}

// =============================================================================
// Private: _launchTextTranslation
// =============================================================================

void PageTranslator::_launchTextTranslation(const QList<TranslatableField> &fields)
{
    const QString prompt = TranslationProtocol::buildPrompt(
        fields, m_currentJob.sourceLang, m_currentJob.targetLang);
    _log(QStringLiteral("  Sending %1 field(s) to claude, prompt size: %2 chars")
             .arg(fields.size()).arg(prompt.size()));

    m_tempDir = std::make_unique<QTemporaryDir>();
    if (!m_tempDir->isValid()) {
        _log(QStringLiteral("  Failed to create temp dir for page %1 → %2")
                 .arg(m_currentJob.pageId).arg(m_currentJob.targetLang), true);
        m_chunkState.reset();
        ++m_errors;
        m_tempDir.reset();
        _processNextJob();
        return;
    }

    const QString promptPath = m_tempDir->path() + QStringLiteral("/prompt.txt");
    {
        QFile f(promptPath);
        if (!f.open(QIODevice::WriteOnly)) {
            _log(QStringLiteral("  Failed to write prompt file for page %1 → %2")
                     .arg(m_currentJob.pageId).arg(m_currentJob.targetLang), true);
            m_chunkState.reset();
            ++m_errors;
            m_tempDir.reset();
            _processNextJob();
            return;
        }
        f.write(prompt.toUtf8());
    }

    m_processOutput.clear();

    m_process = new QProcess(this);
    m_process->setProgram(QStringLiteral("claude"));
    m_process->setArguments({QStringLiteral("-p"), QStringLiteral("-"),
                              QStringLiteral("--dangerously-skip-permissions"),
                              QStringLiteral("--tools"), QStringLiteral(""),
                              QStringLiteral("--output-format"), QStringLiteral("stream-json"),
                              QStringLiteral("--verbose")});
    // Use the temp dir as cwd so the claude CLI finds no project context (no
    // CLAUDE.md, no session history).  Without this, the CLI can inherit the
    // translation project's session history and "continue" a previously
    // interrupted response instead of translating the current prompt fresh.
    m_process->setWorkingDirectory(m_tempDir->path());
    m_process->setStandardInputFile(promptPath);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &PageTranslator::_onProcessReadyRead);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &PageTranslator::_onProcessFinished);

    m_process->start();
}

// =============================================================================
// Private: _finalizeTextTranslations
// =============================================================================

void PageTranslator::_finalizeTextTranslations(const QHash<QString, QString> &translations)
{
    for (auto it = translations.cbegin(); it != translations.cend(); ++it) {
        m_currentPageType->applyTranslation(QStringView{},
                                             it.key(),
                                             m_currentJob.targetLang,
                                             it.value());
    }

    QHash<QString, QString> finalData;
    m_currentPageType->save(finalData);
    for (auto it = m_currentPageData.cbegin(); it != m_currentPageData.cend(); ++it) {
        if (it.key().startsWith(QStringLiteral("__"))) {
            finalData.insert(it.key(), it.value());
        }
    }

    // Normalize and store the translated permalink slug if one was provided.
    if (translations.contains(QStringLiteral("_permalink_slug"))) {
        static const QRegularExpression reInvalidSlugChars(
            QStringLiteral("[^a-z0-9-]"));
        static const QRegularExpression reMultiHyphen(
            QStringLiteral("-{2,}"));
        QString trSlug = translations.value(QStringLiteral("_permalink_slug"))
                             .toLower()
                             .replace(QLatin1Char(' '), QLatin1Char('-'));
        trSlug.replace(reInvalidSlugChars, QString{});
        trSlug.replace(reMultiHyphen, QStringLiteral("-"));
        while (trSlug.startsWith(QLatin1Char('-'))) { trSlug.remove(0, 1); }
        while (trSlug.endsWith(QLatin1Char('-')))   { trSlug.chop(1); }
        if (!trSlug.isEmpty()) {
            const QString key = QStringLiteral("tr:")
                                + m_currentJob.targetLang
                                + QStringLiteral(":_permalink_slug");
            finalData.insert(key, trSlug);
            _log(QStringLiteral("  Slug translated: %1").arg(trSlug));
        }
    }

    m_repo.saveData(m_currentJob.pageId, finalData);

    _log(QStringLiteral("  Page %1 → %2: done (%3 field(s) translated)")
             .arg(m_currentJob.pageId).arg(m_currentJob.targetLang).arg(translations.size()));
    ++m_translated;
    emit pageTranslated(m_currentJob.pageId);

    // Queue SVG translation sub-jobs for any [IMGFIX *.svg] shortcodes in 1_text.
    {
        static const QRegularExpression reImgFix(
            QStringLiteral("\\[IMGFIX\\b[^\\]]*fileName=\"([^\"]+\\.svg)\""),
            QRegularExpression::CaseInsensitiveOption);
        const QString &text1 = m_currentPageData.value(QStringLiteral("1_text"));
        if (!text1.isEmpty()) {
            QList<TranslationJob> svgJobs;
            QStringList seenFns;
            auto it = reImgFix.globalMatch(text1);
            while (it.hasNext()) {
                const QString fn = it.next().captured(1);
                if (!seenFns.contains(fn)) {
                    seenFns.append(fn);
                    TranslationJob svgJob;
                    svgJob.pageId      = m_currentJob.pageId;
                    svgJob.typeId      = m_currentJob.typeId;
                    svgJob.sourceLang  = m_currentJob.sourceLang;
                    svgJob.targetLang  = m_currentJob.targetLang;
                    svgJob.svgFilename = fn;
                    svgJobs.append(svgJob);
                }
            }
            if (!svgJobs.isEmpty()) {
                for (int i = svgJobs.size() - 1; i >= 0; --i) {
                    m_queue.prepend(svgJobs.at(i));
                }
                _log(QStringLiteral("  Queued %1 SVG translation job(s) for page %2 → %3")
                         .arg(svgJobs.size()).arg(m_currentJob.pageId)
                         .arg(m_currentJob.targetLang));
            }
        }
    }

    _processNextJob();
}

// =============================================================================
void PageTranslator::_saveSvgTranslation(const QString    &filename,
                                          const QString    &lang,
                                          const QByteArray &svgData)
{
    const QString dbPath = m_workingDir.filePath(QStringLiteral("images.db"));
    const QString connName = QStringLiteral("transl_svg_write_%1")
                                 .arg(reinterpret_cast<quintptr>(this));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            _log(QStringLiteral("  _saveSvgTranslation: cannot open images.db for %1 → %2")
                     .arg(filename, lang), true);
            return;
        }

        db.transaction();

        // Remove existing translated entry if any.
        {
            QSqlQuery qFind(db);
            qFind.prepare(QStringLiteral(
                "SELECT image_id FROM image_names"
                " WHERE filename = :fn AND domain = :lang LIMIT 1"));
            qFind.bindValue(QStringLiteral(":fn"),   filename);
            qFind.bindValue(QStringLiteral(":lang"), lang);
            if (qFind.exec() && qFind.next()) {
                const int oldId = qFind.value(0).toInt();
                QSqlQuery qDelName(db);
                qDelName.prepare(QStringLiteral(
                    "DELETE FROM image_names WHERE filename = :fn AND domain = :lang"));
                qDelName.bindValue(QStringLiteral(":fn"),   filename);
                qDelName.bindValue(QStringLiteral(":lang"), lang);
                qDelName.exec();
                QSqlQuery qDelImg(db);
                qDelImg.prepare(QStringLiteral("DELETE FROM images WHERE id = :id"));
                qDelImg.bindValue(QStringLiteral(":id"), oldId);
                qDelImg.exec();
            }
        }

        // Insert new blob.
        QSqlQuery qIns(db);
        qIns.prepare(QStringLiteral(
            "INSERT INTO images (blob, mime_type) VALUES (:blob, 'image/svg+xml')"));
        qIns.bindValue(QStringLiteral(":blob"), svgData);
        if (!qIns.exec()) {
            _log(QStringLiteral("  _saveSvgTranslation: INSERT images failed: %1")
                     .arg(qIns.lastError().text()), true);
            db.rollback();
            return;
        }
        const int newId = qIns.lastInsertId().toInt();

        QSqlQuery qName(db);
        qName.prepare(QStringLiteral(
            "INSERT INTO image_names (domain, filename, image_id)"
            " VALUES (:lang, :fn, :id)"));
        qName.bindValue(QStringLiteral(":lang"), lang);
        qName.bindValue(QStringLiteral(":fn"),   filename);
        qName.bindValue(QStringLiteral(":id"),   newId);
        if (!qName.exec()) {
            _log(QStringLiteral("  _saveSvgTranslation: INSERT image_names failed: %1")
                     .arg(qName.lastError().text()), true);
            db.rollback();
            return;
        }

        db.commit();
    }
    QSqlDatabase::removeDatabase(connName);
}

void PageTranslator::_rasterizeSvgToWebPs(const QString    &svgFilename,
                                           const QByteArray &svgData,
                                           const QString    &lang)
{
    // Determine which ImageSize this SVG corresponds to from its filename suffix.
    // Supported suffixes: -og.svg, -wide.svg, -square.svg, -portrait.svg
    static const struct { AbstractSocialMedia::ImageSize size; QLatin1String svgSuffix; } kMap[] = {
        { AbstractSocialMedia::ImageSize::Landscape, QLatin1String("-og.svg")       },
        { AbstractSocialMedia::ImageSize::Wide,      QLatin1String("-wide.svg")     },
        { AbstractSocialMedia::ImageSize::Square,    QLatin1String("-square.svg")   },
        { AbstractSocialMedia::ImageSize::Portrait,  QLatin1String("-portrait.svg") },
    };

    const QString lowerFn = svgFilename.toLower();
    AbstractSocialMedia::ImageSize imgSize = AbstractSocialMedia::ImageSize::Landscape;
    bool found = false;
    for (const auto &entry : kMap) {
        if (lowerFn.endsWith(entry.svgSuffix)) {
            imgSize = entry.size;
            found   = true;
            break;
        }
    }
    if (!found) {
        return; // not a social-media SVG variant — nothing to rasterize
    }

    const QSize dims = AbstractSocialMedia::imageSizeDimensions(imgSize);

    QSvgRenderer renderer(svgData);
    if (!renderer.isValid()) {
        _log(QStringLiteral("  _rasterizeSvgToWebPs: SVG not renderable for %1 → %2")
                 .arg(svgFilename, lang), true);
        return;
    }

    QImage img(dims, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter painter(&img);
    renderer.render(&painter);
    painter.end();

    QBuffer buf;
    buf.open(QBuffer::WriteOnly);
    if (!img.save(&buf, "webp")) {
        _log(QStringLiteral("  _rasterizeSvgToWebPs: WebP encoding failed for %1")
                 .arg(svgFilename), true);
        return;
    }
    const QByteArray webpBlob = buf.data();

    // Derive WebP filename from SVG filename by swapping suffix.
    const QString webpFilename = svgFilename.left(
        svgFilename.size() - 4) + QStringLiteral("webp"); // replace ".svg" with ".webp"

    // Write to images.db under (lang, webpFilename).
    const QString dbPath = m_workingDir.filePath(QStringLiteral("images.db"));
    const QString connName = QStringLiteral("transl_webp_write_%1")
                                 .arg(reinterpret_cast<quintptr>(this));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            _log(QStringLiteral("  _rasterizeSvgToWebPs: cannot open images.db for %1 → %2")
                     .arg(webpFilename, lang), true);
        } else {
            db.transaction();

            // Remove any existing translated WebP for this (lang, filename) pair.
            {
                QSqlQuery qFind(db);
                qFind.prepare(QStringLiteral(
                    "SELECT image_id FROM image_names"
                    " WHERE filename = :fn AND domain = :lang LIMIT 1"));
                qFind.bindValue(QStringLiteral(":fn"),   webpFilename);
                qFind.bindValue(QStringLiteral(":lang"), lang);
                if (qFind.exec() && qFind.next()) {
                    const int oldId = qFind.value(0).toInt();
                    QSqlQuery qDel(db);
                    qDel.prepare(QStringLiteral(
                        "DELETE FROM image_names WHERE filename = :fn AND domain = :lang"));
                    qDel.bindValue(QStringLiteral(":fn"),   webpFilename);
                    qDel.bindValue(QStringLiteral(":lang"), lang);
                    qDel.exec();
                    QSqlQuery qDelImg(db);
                    qDelImg.prepare(QStringLiteral("DELETE FROM images WHERE id = :id"));
                    qDelImg.bindValue(QStringLiteral(":id"), oldId);
                    qDelImg.exec();
                }
            }

            QSqlQuery qIns(db);
            qIns.prepare(QStringLiteral(
                "INSERT INTO images (blob, mime_type) VALUES (:blob, 'image/webp')"));
            qIns.bindValue(QStringLiteral(":blob"), webpBlob);
            if (qIns.exec()) {
                const int newId = qIns.lastInsertId().toInt();
                QSqlQuery qName(db);
                qName.prepare(QStringLiteral(
                    "INSERT INTO image_names (domain, filename, image_id)"
                    " VALUES (:lang, :fn, :id)"));
                qName.bindValue(QStringLiteral(":lang"), lang);
                qName.bindValue(QStringLiteral(":fn"),   webpFilename);
                qName.bindValue(QStringLiteral(":id"),   newId);
                qName.exec();
                db.commit();
                _log(QStringLiteral("  WebP rasterized: %1 → %2 (%3×%4)")
                         .arg(webpFilename, lang).arg(dims.width()).arg(dims.height()));
            } else {
                db.rollback();
                _log(QStringLiteral("  _rasterizeSvgToWebPs: INSERT failed for %1 → %2: %3")
                         .arg(webpFilename, lang, qIns.lastError().text()), true);
            }
        }
    }
    QSqlDatabase::removeDatabase(connName);
}

void PageTranslator::_openLogFile()
{
    const QDir logDir = QDir(m_workingDir.filePath(QStringLiteral("translation_logs")));
    if (!logDir.exists()) {
        m_workingDir.mkpath(QStringLiteral("translation_logs"));
    }

    const QString stamp = QDateTime::currentDateTimeUtc()
                              .toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString filePath = logDir.filePath(
        QStringLiteral("translate_%1.txt").arg(stamp));

    m_logFile = new QFile(filePath, this);
    if (!m_logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        qDebug() << "[Translate] Could not open log file:" << filePath;
        delete m_logFile;
        m_logFile = nullptr;
    } else {
        qDebug() << "[Translate] Log file:" << filePath;
    }
}
