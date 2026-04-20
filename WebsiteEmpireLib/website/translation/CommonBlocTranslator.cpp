#include "CommonBlocTranslator.h"

#include "website/commonblocs/AbstractCommonBloc.h"
#include "website/theme/AbstractTheme.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QTextStream>
#include <QUrl>

static constexpr const char *CLAUDE_API_URL = "https://api.anthropic.com/v1/messages";
static constexpr const char *CLAUDE_MODEL   = "claude-sonnet-4-6";

// =============================================================================
// Constructor / Destructor
// =============================================================================

CommonBlocTranslator::CommonBlocTranslator(AbstractTheme &theme,
                                             const QDir    &workingDir,
                                             QObject       *parent)
    : QObject(parent)
    , m_theme(theme)
    , m_workingDir(workingDir)
{
    m_apiKey = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("ANTHROPIC_API_KEY"));
}

CommonBlocTranslator::~CommonBlocTranslator()
{
    if (m_logFile && m_logFile->isOpen()) {
        m_logFile->close();
    }
    delete m_logFile;
}

// =============================================================================
// buildJobs (static)
// =============================================================================

QList<CommonBlocTranslator::TranslationJob>
CommonBlocTranslator::buildJobs(const QList<AbstractCommonBloc *> &blocs,
                                 const QString                     &sourceLang,
                                 const QStringList                 &targetLangs)
{
    QList<TranslationJob> jobs;
    for (AbstractCommonBloc *bloc : std::as_const(blocs)) {
        if (!bloc || bloc->sourceTexts().isEmpty()) {
            continue;
        }
        for (const QString &targetLang : std::as_const(targetLangs)) {
            if (targetLang == sourceLang) {
                continue;
            }
            if (!bloc->missingTranslations(targetLang, sourceLang).isEmpty()) {
                TranslationJob job;
                job.blocId     = bloc->getId();
                job.sourceLang = sourceLang;
                job.targetLang = targetLang;
                jobs.append(job);
            }
        }
    }
    return jobs;
}

// =============================================================================
// startWithJobs
// =============================================================================

void CommonBlocTranslator::startWithJobs(const QList<TranslationJob> &jobs)
{
    _openLogFile();

    if (m_apiKey.isEmpty()) {
        _log(QStringLiteral("ERROR: ANTHROPIC_API_KEY environment variable is not set."), true);
        emit finished(0, 1);
        return;
    }

    m_queue = jobs;
    _log(QStringLiteral("Common bloc translation started. %1 job(s) queued.")
             .arg(m_queue.size()));

    if (m_queue.isEmpty()) {
        _log(QStringLiteral("Nothing to translate. All common bloc fields are up to date."));
        emit finished(0, 0);
        return;
    }

    m_nam = new QNetworkAccessManager(this);
    _processNextJob();
}

// =============================================================================
// Private: _processNextJob
// =============================================================================

void CommonBlocTranslator::_processNextJob()
{
    if (m_queue.isEmpty()) {
        _log(QStringLiteral("All jobs done. Translated: %1  Errors: %2")
                 .arg(m_translated).arg(m_errors));
        emit finished(m_translated, m_errors);
        return;
    }

    m_currentJob  = m_queue.takeFirst();
    m_currentBloc = nullptr;

    _log(QStringLiteral("Processing bloc '%1' → %2 …")
             .arg(m_currentJob.blocId, m_currentJob.targetLang));

    // Find the bloc by stable ID in the theme.
    const QList<AbstractCommonBloc *> allBlocs =
        m_theme.getTopBlocs() + m_theme.getBottomBlocs();
    for (AbstractCommonBloc *b : std::as_const(allBlocs)) {
        if (b && b->getId() == m_currentJob.blocId) {
            m_currentBloc = b;
            break;
        }
    }

    if (!m_currentBloc) {
        _log(QStringLiteral("  Skipping: bloc '%1' not found in theme.")
                 .arg(m_currentJob.blocId), true);
        ++m_errors;
        _processNextJob();
        return;
    }

    // Collect only the fields that still need translation.
    const QHash<QString, QString> &sources = m_currentBloc->sourceTexts();
    QList<TranslatableField> fields;
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        if (it.value().isEmpty()) {
            continue;
        }
        if (!m_currentBloc->translatedText(it.key(), m_currentJob.targetLang).isEmpty()) {
            continue; // already translated
        }
        TranslatableField f;
        f.id         = it.key();
        f.sourceText = it.value();
        fields.append(f);
    }

    if (fields.isEmpty()) {
        _log(QStringLiteral("  Bloc '%1' → %2 already fully translated — skipping.")
                 .arg(m_currentJob.blocId, m_currentJob.targetLang));
        _processNextJob();
        return;
    }

    const QString &prompt = _buildPrompt(fields,
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
    req.setRawHeader(QByteArrayLiteral("x-api-key"),         m_apiKey.toUtf8());
    req.setRawHeader(QByteArrayLiteral("anthropic-version"), QByteArrayLiteral("2023-06-01"));

    m_reply = m_nam->post(req, bodyBytes);
    connect(m_reply, &QNetworkReply::finished,
            this,    &CommonBlocTranslator::_onReplyFinished);
}

// =============================================================================
// Private: _onReplyFinished
// =============================================================================

void CommonBlocTranslator::_onReplyFinished()
{
    const QByteArray raw    = m_reply->readAll();
    const auto       netErr = m_reply->error();
    m_reply->deleteLater();
    m_reply = nullptr;

    if (netErr != QNetworkReply::NoError) {
        _log(QStringLiteral("  Network error for bloc '%1' → %2")
                 .arg(m_currentJob.blocId, m_currentJob.targetLang), true);
        ++m_errors;
        _processNextJob();
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (doc.isNull()) {
        _log(QStringLiteral("  Invalid JSON response for bloc '%1' → %2")
                 .arg(m_currentJob.blocId, m_currentJob.targetLang), true);
        ++m_errors;
        _processNextJob();
        return;
    }

    const QJsonArray content = doc.object()[QStringLiteral("content")].toArray();
    if (content.isEmpty()) {
        const QString apiErr = doc.object()[QStringLiteral("error")]
                                   .toObject()[QStringLiteral("message")]
                                   .toString();
        _log(QStringLiteral("  API error for bloc '%1' → %2: %3")
                 .arg(m_currentJob.blocId, m_currentJob.targetLang,
                      apiErr.isEmpty() ? QStringLiteral("(empty content)") : apiErr),
             true);
        ++m_errors;
        _processNextJob();
        return;
    }

    const QString translatedJson =
        content.at(0).toObject()[QStringLiteral("text")].toString();
    const QHash<QString, QString> &translations = _parseResponse(translatedJson);

    if (translations.isEmpty()) {
        _log(QStringLiteral("  Could not parse translation JSON for bloc '%1' → %2")
                 .arg(m_currentJob.blocId, m_currentJob.targetLang), true);
        ++m_errors;
        _processNextJob();
        return;
    }

    // Apply translations to the in-memory bloc and persist immediately.
    if (m_currentBloc) {
        for (auto it = translations.cbegin(); it != translations.cend(); ++it) {
            m_currentBloc->setTranslation(it.key(), m_currentJob.targetLang, it.value());
        }
        m_theme.saveBlocsData();
    }

    _log(QStringLiteral("  Bloc '%1' → %2: done (%3 field(s) translated)")
             .arg(m_currentJob.blocId, m_currentJob.targetLang)
             .arg(translations.size()));
    ++m_translated;
    emit blocTranslated(m_currentJob.blocId, m_currentJob.targetLang);

    _processNextJob();
}

// =============================================================================
// Private helpers
// =============================================================================

QString CommonBlocTranslator::_buildPrompt(const QList<TranslatableField> &fields,
                                            const QString                  &sourceLang,
                                            const QString                  &targetLang) const
{
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
               "Translate the following common bloc fields from %1 to %2.\n\n"
               "Each field has an \"id\" (opaque key, never translate it) and "
               "a \"source\" (the text to translate).\n\n"
               "Rules:\n"
               "- Return ONLY a valid JSON object mapping each \"id\" to its "
               "translated text — no markdown fences, no explanation\n"
               "- Preserve any HTML tags in the source; translate only the text content\n"
               "- Keep proper nouns, brand names, and technical terms appropriate\n\n"
               "Fields:\n%3\n\n"
               "JSON output (example for two fields):\n"
               "{\"title\": \"translated title\", \"subtitle\": \"translated subtitle\"}")
        .arg(sourceLang, targetLang, fieldsJson);
}

QHash<QString, QString>
CommonBlocTranslator::_parseResponse(const QString &jsonText) const
{
    QHash<QString, QString> result;

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

void CommonBlocTranslator::_log(const QString &msg, bool errorLevel)
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

void CommonBlocTranslator::_openLogFile()
{
    const QDir logDir = QDir(m_workingDir.filePath(QStringLiteral("translation_logs")));
    if (!logDir.exists()) {
        m_workingDir.mkpath(QStringLiteral("translation_logs"));
    }

    const QString stamp    = QDateTime::currentDateTimeUtc()
                                 .toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString filePath = logDir.filePath(
        QStringLiteral("translate_common_%1.txt").arg(stamp));

    m_logFile = new QFile(filePath, this);
    if (!m_logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        qDebug() << "[TranslateCommon] Could not open log file:" << filePath;
        delete m_logFile;
        m_logFile = nullptr;
    } else {
        qDebug() << "[TranslateCommon] Log file:" << filePath;
    }
}
