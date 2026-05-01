#include "PageTranslator.h"

#include "website/AbstractEngine.h"
#include "website/pages/AbstractLegalPageDef.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageTypeLegal.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/translation/TranslationProtocol.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QProcess>
#include <QRegularExpression>
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
    _log(QStringLiteral("Processing page %1 → %2 (job %3 of original batch)…")
             .arg(m_currentJob.pageId)
             .arg(m_currentJob.targetLang)
             .arg(m_translated + m_errors + 1));

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

    if (fields.isEmpty()) {
        _log(QStringLiteral("  Page %1 has no translatable content — skipping")
                 .arg(m_currentJob.pageId));
        _processNextJob();
        return;
    }

    m_currentSingleFieldId = (fields.size() == 1) ? fields.first().id : QString{};

    const QString prompt = TranslationProtocol::buildPrompt(fields, m_currentJob.sourceLang, m_currentJob.targetLang);
    _log(QStringLiteral("  Sending %1 field(s) to claude, prompt size: %2 chars")
             .arg(fields.size()).arg(prompt.size()));

    // Write prompt to a temp file and launch the claude CLI.
    m_tempDir = std::make_unique<QTemporaryDir>();
    if (!m_tempDir->isValid()) {
        _log(QStringLiteral("  Failed to create temp dir for page %1 → %2")
                 .arg(m_currentJob.pageId).arg(m_currentJob.targetLang), true);
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
    m_process->setStandardInputFile(promptPath);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &PageTranslator::_onProcessReadyRead);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &PageTranslator::_onProcessFinished);

    m_process->start();
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
        const QString err = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
        _log(QStringLiteral("  claude error for page %1 → %2: %3")
                 .arg(m_currentJob.pageId).arg(m_currentJob.targetLang,
                      err.isEmpty() ? QStringLiteral("exit code %1").arg(exitCode) : err),
             true);
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
        ++m_errors;
        _processNextJob();
        return;
    }

    QHash<QString, QString> translations = TranslationProtocol::parseResponse(translatedJson);

    // Single-field fallback: Claude sometimes omits the ===BEGIN header for very
    // long responses. When only one field was sent and parsing finds nothing, strip
    // any trailing ===END=== marker and treat the entire response as that field's
    // translation.
    if (translations.isEmpty() && !m_currentSingleFieldId.isEmpty() && !translatedJson.isEmpty()) {
        static const QRegularExpression reStripEnd(
            QStringLiteral(R"(\s*===END===\s*$)"),
            QRegularExpression::DotMatchesEverythingOption);
        QString cleaned = translatedJson;
        cleaned.remove(reStripEnd);
        cleaned = cleaned.trimmed();
        if (!cleaned.isEmpty()) {
            translations.insert(m_currentSingleFieldId, cleaned);
            _log(QStringLiteral("  Page %1 → %2: ===BEGIN missing — using full response as single-field translation (%3 chars)")
                     .arg(m_currentJob.pageId).arg(m_currentJob.targetLang).arg(cleaned.size()));
        }
    }

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
        ++m_errors;
        _processNextJob();
        return;
    }

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
    m_repo.saveData(m_currentJob.pageId, finalData);

    m_processOutput.clear();

    _log(QStringLiteral("  Page %1 → %2: done (%3 field(s) translated)")
             .arg(m_currentJob.pageId).arg(m_currentJob.targetLang).arg(translations.size()));
    ++m_translated;
    emit pageTranslated(m_currentJob.pageId);

    _processNextJob();
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
