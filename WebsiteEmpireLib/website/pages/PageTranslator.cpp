#include "PageTranslator.h"

#include "website/AbstractEngine.h"
#include "website/pages/AbstractLegalPageDef.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageTypeLegal.h"
#include "website/pages/attributes/CategoryTable.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QTextStream>
#include <QUrl>

static constexpr const char *CLAUDE_API_URL =
    "https://api.anthropic.com/v1/messages";
static constexpr const char *CLAUDE_MODEL   =
    "claude-sonnet-4-6";

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
    m_apiKey = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("ANTHROPIC_API_KEY"));
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

    if (m_apiKey.isEmpty()) {
        _log(QStringLiteral("ERROR: ANTHROPIC_API_KEY environment variable is not set."), true);
        emit finished(0, 1);
        return;
    }
    if (!engine) {
        _log(QStringLiteral("ERROR: No engine provided."), true);
        emit finished(0, 1);
        return;
    }
    if (editingLang.isEmpty()) {
        _log(QStringLiteral("ERROR: Editing language is not configured."), true);
        emit finished(0, 1);
        return;
    }

    m_queue = _buildJobQueue(engine, editingLang);
    _log(QStringLiteral("Translation started. %1 job(s) queued.").arg(m_queue.size()));

    if (m_queue.isEmpty()) {
        _log(QStringLiteral("Nothing to translate. All pages are up to date."));
        emit finished(0, 0);
        return;
    }

    m_nam = new QNetworkAccessManager(this);
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
    ts << QStringLiteral("API endpoint : ") << QLatin1String(CLAUDE_API_URL) << QStringLiteral("\n");
    ts << QStringLiteral("Model        : ") << QLatin1String(CLAUDE_MODEL)   << QStringLiteral("\n\n");

    int n = 1;
    for (const TranslationJob &job : std::as_const(jobs)) {
        const auto &srcRec = m_repo.findById(job.sourcePageId);
        const QString srcTitle = srcRec ? srcRec->permalink : QStringLiteral("?");

        ts << QStringLiteral("--- Job %1: \"%2\"  (%3 → %4) ---\n")
                  .arg(n++)
                  .arg(srcTitle, job.sourceLang, job.targetLang);

        const QHash<QString, QString> &data = m_repo.loadData(job.sourcePageId);
        if (data.isEmpty()) {
            ts << tr("  (source page has no data)\n\n");
            continue;
        }

        ts << QStringLiteral("User message to send to Claude:\n\n");
        ts << _buildPrompt(data, job.sourceLang, job.targetLang);
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

    // Collect distinct target language codes from the engine.
    QStringList targetLangs;
    const int rows = engine->rowCount();
    for (int i = 0; i < rows; ++i) {
        const QString lang = engine->data(
            engine->index(i, AbstractEngine::COL_LANG_CODE)).toString();
        if (!lang.isEmpty() && lang != editingLang && !targetLangs.contains(lang)) {
            targetLangs.append(lang);
        }
    }

    if (targetLangs.isEmpty()) {
        _log(QStringLiteral("No target languages found in engine (only editing lang '%1' configured).")
                 .arg(editingLang));
        return queue;
    }

    _log(QStringLiteral("Source lang: %1 | Target langs: %2")
             .arg(editingLang, targetLangs.join(QStringLiteral(", "))));

    const QList<PageRecord> &sources = m_repo.findSourcePages(editingLang);
    _log(QStringLiteral("Source pages found: %1").arg(sources.size()));

    for (const PageRecord &src : std::as_const(sources)) {
        const QList<PageRecord> &existing = m_repo.findTranslations(src.id);

        for (const QString &targetLang : std::as_const(targetLangs)) {
            // Find existing translation for this target lang (if any).
            const PageRecord *existingTrans = nullptr;
            for (const PageRecord &t : std::as_const(existing)) {
                if (t.lang == targetLang) {
                    existingTrans = &t;
                    break;
                }
            }

            bool needsWork = false;
            int  targetPageId = 0;

            if (!existingTrans) {
                // No translation page yet — create one during processing.
                needsWork   = true;
                targetPageId = 0;
                _log(QStringLiteral("  Page %1 (%2): no %3 translation yet → will create")
                         .arg(src.id).arg(src.permalink, targetLang));
            } else if (existingTrans->translatedAt.isEmpty()) {
                // Translation page exists but AI has not run yet.
                needsWork    = true;
                targetPageId = existingTrans->id;
                _log(QStringLiteral("  Page %1 (%2): %3 translation #%4 not yet AI-translated → will translate")
                         .arg(src.id).arg(src.permalink, targetLang).arg(existingTrans->id));
            } else if (src.updatedAt > existingTrans->translatedAt) {
                // Source was updated after last AI translation.
                needsWork    = true;
                targetPageId = existingTrans->id;
                _log(QStringLiteral("  Page %1 (%2): source updated (%3) after last translation (%4) → re-translate")
                         .arg(src.id).arg(src.permalink, src.updatedAt, existingTrans->translatedAt));
            } else {
                _log(QStringLiteral("  Page %1 (%2): %3 translation up to date — skipping")
                         .arg(src.id).arg(src.permalink, targetLang));
            }

            if (needsWork) {
                // Compute a stable placeholder permalink for new translation pages.
                const QString fileName = QFileInfo(src.permalink).fileName();
                const QString targetPermalink =
                    QStringLiteral("/%1/%2").arg(targetLang, fileName.isEmpty()
                                                                  ? QStringLiteral("index.html")
                                                                  : fileName);

                TranslationJob job;
                job.sourcePageId    = src.id;
                job.targetPageId    = targetPageId;
                job.typeId          = src.typeId;
                job.sourceLang      = editingLang;
                job.targetLang      = targetLang;
                job.targetPermalink = targetPermalink;
                queue.append(job);
            }
        }
    }

    // Legal pages must always be translated first so they are available for
    // review before any other content is published.
    std::stable_sort(queue.begin(), queue.end(),
                     [](const TranslationJob &a, const TranslationJob &b) {
                         const bool aLegal = (a.typeId == QLatin1String(PageTypeLegal::TYPE_ID));
                         const bool bLegal = (b.typeId == QLatin1String(PageTypeLegal::TYPE_ID));
                         return aLegal && !bLegal;
                     });

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
        emit finished(m_translated, m_errors);
        return;
    }

    m_currentJob = m_queue.takeFirst();
    _log(QStringLiteral("Translating page %1 → %2 (job %3 of original batch)…")
             .arg(m_currentJob.sourcePageId)
             .arg(m_currentJob.targetLang)
             .arg(m_translated + m_errors + 1));

    const QHash<QString, QString> &srcData = m_repo.loadData(m_currentJob.sourcePageId);
    if (srcData.isEmpty()) {
        _log(QStringLiteral("  Skipping: source page %1 has no data.")
                 .arg(m_currentJob.sourcePageId));
        _processNextJob();
        return;
    }

    const QString &prompt = _buildPrompt(srcData,
                                          m_currentJob.sourceLang,
                                          m_currentJob.targetLang);

    // Build Claude API request body.
    QJsonObject msgObj;
    msgObj[QStringLiteral("role")]    = QStringLiteral("user");
    msgObj[QStringLiteral("content")] = prompt;

    QJsonObject body;
    body[QStringLiteral("model")]      = QLatin1String(CLAUDE_MODEL);
    body[QStringLiteral("max_tokens")] = 4096;
    body[QStringLiteral("messages")]   = QJsonArray{msgObj};

    const QByteArray bodyBytes = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest req{QUrl(QLatin1String(CLAUDE_API_URL))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    req.setRawHeader(QByteArrayLiteral("x-api-key"),           m_apiKey.toUtf8());
    req.setRawHeader(QByteArrayLiteral("anthropic-version"),   QByteArrayLiteral("2023-06-01"));

    m_reply = m_nam->post(req, bodyBytes);
    connect(m_reply, &QNetworkReply::finished, this, &PageTranslator::_onReplyFinished);
}

// =============================================================================
// Private: _onReplyFinished
// =============================================================================

void PageTranslator::_onReplyFinished()
{
    const QByteArray raw = m_reply->readAll();
    const QNetworkReply::NetworkError netErr = m_reply->error();
    m_reply->deleteLater();
    m_reply = nullptr;

    if (netErr != QNetworkReply::NoError) {
        _log(QStringLiteral("  Network error for page %1 → %2: %3")
                 .arg(m_currentJob.sourcePageId)
                 .arg(m_currentJob.targetLang, m_reply ? m_reply->errorString()
                                                        : QStringLiteral("(reply gone)")),
             true);
        ++m_errors;
        _processNextJob();
        return;
    }

    // Parse API response: {"content": [{"type": "text", "text": "..."}], ...}
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (doc.isNull()) {
        _log(QStringLiteral("  Invalid JSON response for page %1 → %2")
                 .arg(m_currentJob.sourcePageId).arg(m_currentJob.targetLang), true);
        ++m_errors;
        _processNextJob();
        return;
    }

    const QJsonArray content = doc.object()[QStringLiteral("content")].toArray();
    if (content.isEmpty()) {
        const QString apiErr = doc.object()[QStringLiteral("error")]
                                    .toObject()[QStringLiteral("message")]
                                    .toString();
        _log(QStringLiteral("  API error for page %1 → %2: %3")
                 .arg(m_currentJob.sourcePageId).arg(m_currentJob.targetLang,
                                                      apiErr.isEmpty() ? QStringLiteral("(empty content)")
                                                                        : apiErr),
             true);
        ++m_errors;
        _processNextJob();
        return;
    }

    const QString translatedJson = content.at(0).toObject()[QStringLiteral("text")].toString();
    const QHash<QString, QString> &translatedData = _parseResponse(translatedJson);

    if (translatedData.isEmpty()) {
        _log(QStringLiteral("  Could not parse translation JSON for page %1 → %2")
                 .arg(m_currentJob.sourcePageId).arg(m_currentJob.targetLang), true);
        ++m_errors;
        _processNextJob();
        return;
    }

    // Ensure a translation page record exists.
    int targetId = m_currentJob.targetPageId;
    if (targetId == 0) {
        targetId = m_repo.createTranslation(m_currentJob.sourcePageId,
                                             m_currentJob.typeId,
                                             m_currentJob.targetPermalink,
                                             m_currentJob.targetLang);
        _log(QStringLiteral("  Created translation page id=%1 at %2")
                 .arg(targetId).arg(m_currentJob.targetPermalink));
    }

    // Re-inject internal (__-prefixed) keys from the source page.  These keys
    // are excluded from the translation prompt and must be copied verbatim so
    // that stamps like __legal_def_id survive across translation passes.
    const QHash<QString, QString> &srcDataForKeys = m_repo.loadData(m_currentJob.sourcePageId);
    QHash<QString, QString> finalData = translatedData;
    for (auto it = srcDataForKeys.cbegin(); it != srcDataForKeys.cend(); ++it) {
        if (it.key().startsWith(QStringLiteral("__"))) {
            finalData.insert(it.key(), it.value());
        }
    }

    m_repo.saveData(targetId, finalData);
    m_repo.setTranslatedAt(targetId, QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    _log(QStringLiteral("  Page %1 → %2: done (target id=%3)")
             .arg(m_currentJob.sourcePageId).arg(m_currentJob.targetLang).arg(targetId));
    ++m_translated;
    emit pageTranslated(targetId);

    _processNextJob();
}

// =============================================================================
// Private helpers
// =============================================================================

QString PageTranslator::_buildPrompt(const QHash<QString, QString> &data,
                                      const QString &sourceLang,
                                      const QString &targetLang) const
{
    // Serialise the page data to JSON for the prompt.
    // Keys prefixed with __ are internal system stamps (e.g. __legal_def_id);
    // they must not be translated and are excluded from the prompt.
    QJsonObject obj;
    for (auto it = data.cbegin(); it != data.cend(); ++it) {
        if (!it.key().startsWith(QStringLiteral("__"))) {
            obj[it.key()] = it.value();
        }
    }
    const QString jsonStr = QString::fromUtf8(
        QJsonDocument(obj).toJson(QJsonDocument::Indented));

    return QStringLiteral(
               "Translate the following page content from %1 to %2.\n\n"
               "The content is a flat key-value map where keys identify page "
               "structure (e.g. \"0_title\", \"1_body\") and values are the text.\n\n"
               "Rules:\n"
               "- Translate ONLY the values, preserve all keys exactly as-is\n"
               "- Preserve any HTML tags in values; translate only the text content\n"
               "- Keep proper nouns, brand names, and technical terms appropriate\n"
               "- Return ONLY a valid JSON object — no markdown fences, no explanation\n\n"
               "Content:\n%3")
        .arg(sourceLang, targetLang, jsonStr);
}

QHash<QString, QString> PageTranslator::_parseResponse(const QString &jsonText) const
{
    QHash<QString, QString> result;

    // Strip a possible markdown code block wrapper (```json ... ```)
    QString clean = jsonText.trimmed();
    if (clean.startsWith(QStringLiteral("```"))) {
        const int firstNewline = clean.indexOf(QLatin1Char('\n'));
        const int lastFence    = clean.lastIndexOf(QStringLiteral("```"));
        if (firstNewline > 0 && lastFence > firstNewline) {
            clean = clean.mid(firstNewline + 1, lastFence - firstNewline - 1).trimmed();
        }
    }

    const QJsonDocument doc = QJsonDocument::fromJson(clean.toUtf8());
    if (!doc.isObject()) {
        return result;
    }

    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        result.insert(it.key(), it.value().toString());
    }
    return result;
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
