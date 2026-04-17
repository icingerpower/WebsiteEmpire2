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
// startWithJobs
// =============================================================================

void PageTranslator::startWithJobs(const QList<TranslationJob> &jobs)
{
    _openLogFile();

    if (m_apiKey.isEmpty()) {
        _log(QStringLiteral("ERROR: ANTHROPIC_API_KEY environment variable is not set."), true);
        emit finished(0, 1);
        return;
    }

    m_queue = jobs;
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
        ts << _buildPrompt(fields, job.sourceLang, job.targetLang);
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

    // Source pages are those authored in editingLang.
    const QList<PageRecord> &sources = m_repo.findSourcePages(editingLang);
    _log(QStringLiteral("Source pages found: %1").arg(sources.size()));

    for (const PageRecord &src : std::as_const(sources)) {
        if (src.langCodesToTranslate.isEmpty()) {
            _log(QStringLiteral("  Page %1 (%2): no target languages set — skipping")
                     .arg(src.id).arg(src.permalink));
            continue;
        }

        for (const QString &targetLang : std::as_const(src.langCodesToTranslate)) {
            // Queue all requested (page × lang) pairs.
            // Completeness is checked cheaply in _processNextJob() before each
            // API call so we skip already-translated fields without pre-loading
            // every page's data during queue construction.
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
    _log(QStringLiteral("Processing page %1 → %2 (job %3 of original batch)…")
             .arg(m_currentJob.pageId)
             .arg(m_currentJob.targetLang)
             .arg(m_translated + m_errors + 1));

    // Load and initialise the page type.
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

    // Skip the API call when the translation is already complete.
    if (m_currentPageType->isTranslationComplete(QStringView{}, m_currentJob.targetLang)) {
        _log(QStringLiteral("  Page %1 already fully translated into %2 — skipping")
                 .arg(m_currentJob.pageId).arg(m_currentJob.targetLang));
        _processNextJob();
        return;
    }

    // Collect only the fields that still need translation.
    QList<TranslatableField> allFields;
    m_currentPageType->collectTranslatables(QStringView{}, allFields);

    // Filter to untranslated fields only to minimise prompt size.
    QList<TranslatableField> pendingFields;
    for (const TranslatableField &f : std::as_const(allFields)) {
        // A field needs translation when it has source text but no translation
        // yet.  We query the page type: if removing just this field would make
        // the translation incomplete, it is pending.  Instead of that complex
        // check, we simply include all fields and let the AI re-confirm already-
        // translated ones — the response is idempotent (applyTranslation
        // overwrites existing values with equivalent text).
        pendingFields.append(f);
    }

    if (pendingFields.isEmpty()) {
        _log(QStringLiteral("  Page %1 has no translatable content — skipping")
                 .arg(m_currentJob.pageId));
        _processNextJob();
        return;
    }

    const QString &prompt = _buildPrompt(pendingFields,
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
        _log(QStringLiteral("  Network error for page %1 → %2")
                 .arg(m_currentJob.pageId)
                 .arg(m_currentJob.targetLang),
             true);
        ++m_errors;
        _processNextJob();
        return;
    }

    // Parse API response: {"content": [{"type": "text", "text": "..."}], ...}
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (doc.isNull()) {
        _log(QStringLiteral("  Invalid JSON response for page %1 → %2")
                 .arg(m_currentJob.pageId).arg(m_currentJob.targetLang), true);
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
                 .arg(m_currentJob.pageId).arg(m_currentJob.targetLang,
                                                apiErr.isEmpty() ? QStringLiteral("(empty content)")
                                                                  : apiErr),
             true);
        ++m_errors;
        _processNextJob();
        return;
    }

    const QString translatedJson = content.at(0).toObject()[QStringLiteral("text")].toString();
    const QHash<QString, QString> &translations = _parseResponse(translatedJson);

    if (translations.isEmpty()) {
        _log(QStringLiteral("  Could not parse translation JSON for page %1 → %2")
                 .arg(m_currentJob.pageId).arg(m_currentJob.targetLang), true);
        ++m_errors;
        _processNextJob();
        return;
    }

    // Apply each translated field to the in-memory page type.
    for (auto it = translations.cbegin(); it != translations.cend(); ++it) {
        m_currentPageType->applyTranslation(QStringView{},
                                             it.key(),
                                             m_currentJob.targetLang,
                                             it.value());
    }

    // Persist: save() serialises bloc data + embedded translations.
    // Re-inject internal (__-prefixed) keys that blocs do not own.
    QHash<QString, QString> finalData;
    m_currentPageType->save(finalData);
    for (auto it = m_currentPageData.cbegin(); it != m_currentPageData.cend(); ++it) {
        if (it.key().startsWith(QStringLiteral("__"))) {
            finalData.insert(it.key(), it.value());
        }
    }
    m_repo.saveData(m_currentJob.pageId, finalData);

    _log(QStringLiteral("  Page %1 → %2: done (%3 field(s) translated)")
             .arg(m_currentJob.pageId).arg(m_currentJob.targetLang).arg(translations.size()));
    ++m_translated;
    emit pageTranslated(m_currentJob.pageId);

    _processNextJob();
}

// =============================================================================
// Private helpers
// =============================================================================

QString PageTranslator::_buildPrompt(const QList<TranslatableField> &fields,
                                      const QString &sourceLang,
                                      const QString &targetLang) const
{
    // Serialise fields as a JSON array: [{id, source}, ...]
    QJsonArray arr;
    for (const TranslatableField &f : std::as_const(fields)) {
        QJsonObject obj;
        obj[QStringLiteral("id")]     = f.id;
        obj[QStringLiteral("source")] = f.sourceText;
        arr.append(obj);
    }
    const QString fieldsJson = QString::fromUtf8(
        QJsonDocument(arr).toJson(QJsonDocument::Indented));

    return QStringLiteral(
               "Translate the following page fields from %1 to %2.\n\n"
               "Each field has an \"id\" (opaque key, never translate it) and "
               "a \"source\" (the text to translate).\n\n"
               "Rules:\n"
               "- Return ONLY a valid JSON object mapping each \"id\" to its "
               "translated text — no markdown fences, no explanation\n"
               "- Preserve any HTML tags in the source; translate only the text content\n"
               "- Keep proper nouns, brand names, and technical terms appropriate\n\n"
               "Fields:\n%3\n\n"
               "JSON output (example for two fields):\n"
               "{\"0_text\": \"translated text\", \"1_title\": \"translated title\"}")
        .arg(sourceLang, targetLang, fieldsJson);
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
