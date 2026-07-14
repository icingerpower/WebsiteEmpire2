#include "HubSeoTranslator.h"

#include "aicli/AbstractCli.h"

#include "TranslationProtocol.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>

// =============================================================================
// Constructor / Destructor
// =============================================================================

HubSeoTranslator::HubSeoTranslator(const QDir  &workingDir,
                                     AbstractCli *cli,
                                     QObject     *parent)
    : QObject(parent)
    , m_db(workingDir)
    , m_workingDir(workingDir)
    , m_cli(cli)
{
}

HubSeoTranslator::~HubSeoTranslator()
{
    if (m_logFile && m_logFile->isOpen()) {
        m_logFile->close();
    }
    delete m_logFile;
}

// =============================================================================
// buildJobs
// =============================================================================

QList<HubSeoTranslator::TranslationJob>
HubSeoTranslator::buildJobs(const QMap<QString, QMap<QString, QString>> &pageTypeTemplates,
                              const QString                               &sourceLang,
                              const QStringList                           &targetLangs) const
{
    // Build a fast-lookup set of already-stored (typeId|key|langCode) triples.
    const QList<HubSeoTemplateDb::StoredKey> storedKeys = m_db.allStoredKeys();
    QSet<QString> storedSet;
    storedSet.reserve(storedKeys.size());
    for (const HubSeoTemplateDb::StoredKey &sk : std::as_const(storedKeys)) {
        storedSet.insert(sk.typeId + QLatin1Char('|') + sk.key + QLatin1Char('|') + sk.langCode);
    }

    QList<TranslationJob> jobs;

    for (auto typeIt = pageTypeTemplates.cbegin(); typeIt != pageTypeTemplates.cend(); ++typeIt) {
        const QString &typeId = typeIt.key();
        const QMap<QString, QString> &templates = typeIt.value();
        if (templates.isEmpty()) {
            continue;
        }

        for (const QString &targetLang : std::as_const(targetLangs)) {
            if (targetLang == sourceLang) {
                continue;
            }

            QList<TranslatableField> fields;
            for (auto tmplIt = templates.cbegin(); tmplIt != templates.cend(); ++tmplIt) {
                const QString &key = tmplIt.key();
                const QString storedKey =
                    typeId + QLatin1Char('|') + key + QLatin1Char('|') + targetLang;
                if (!storedSet.contains(storedKey)) {
                    TranslatableField f;
                    f.id         = key;
                    f.sourceText = tmplIt.value();
                    fields.append(f);
                }
            }

            if (!fields.isEmpty()) {
                TranslationJob job;
                job.typeId     = typeId;
                job.targetLang = targetLang;
                job.fields     = std::move(fields);
                jobs.append(job);
            }
        }
    }

    return jobs;
}

// =============================================================================
// startWithJobs
// =============================================================================

void HubSeoTranslator::startWithJobs(const QList<TranslationJob> &jobs)
{
    _openLogFile();

    m_queue = jobs;
    _log(QStringLiteral("Hub SEO template translation started. %1 job(s) queued.")
             .arg(m_queue.size()));

    if (m_queue.isEmpty()) {
        _log(QStringLiteral("Nothing to translate. All hub SEO templates are up to date."));
        _emitFinished(0, 0);
        return;
    }

    _processNextJob();
}

// =============================================================================
// Private: _processNextJob
// =============================================================================

void HubSeoTranslator::_processNextJob()
{
    if (m_queue.isEmpty()) {
        _log(QStringLiteral("All jobs done. Translated: %1  Errors: %2")
                 .arg(m_translated).arg(m_errors));
        _emitFinished(m_translated, m_errors);
        return;
    }

    m_currentJob = m_queue.takeFirst();

    _log(QStringLiteral("Processing %1 hub SEO template(s) [%2] -> %3 ...")
             .arg(m_currentJob.fields.size())
             .arg(m_currentJob.typeId, m_currentJob.targetLang));

    // Prepend context about %N placeholders so Claude preserves them.
    const QString context =
        QStringLiteral("Context: These are SEO template strings. "
                        "Keep all %1, %2, %3 Qt-style placeholders exactly as-is "
                        "in the translated output — they will be replaced at runtime "
                        "with dynamic values like names and counts.\n\n");

    const QString prompt = TranslationProtocol::buildPrompt(
        m_currentJob.fields, QStringLiteral("en"), m_currentJob.targetLang);

    const QString fullPrompt = context + prompt;

    m_tempDir = std::make_unique<QTemporaryDir>();
    if (!m_tempDir->isValid()) {
        _log(QStringLiteral("  Failed to create temp dir for hub SEO [%1] -> %2")
                 .arg(m_currentJob.typeId, m_currentJob.targetLang), true);
        ++m_errors;
        m_tempDir.reset();
        _processNextJob();
        return;
    }

    const QString promptPath = m_tempDir->path() + QStringLiteral("/prompt.txt");
    {
        QFile f(promptPath);
        if (!f.open(QIODevice::WriteOnly)) {
            _log(QStringLiteral("  Failed to write prompt file for hub SEO [%1] -> %2")
                     .arg(m_currentJob.typeId, m_currentJob.targetLang), true);
            ++m_errors;
            m_tempDir.reset();
            _processNextJob();
            return;
        }
        f.write(fullPrompt.toUtf8());
    }

    m_processOutput.clear();

    m_process = new QProcess(this);
    m_process->setProgram(m_cli->getExecutable());
    m_cli->configurePromptProcess(m_process, m_cli->translationPromptArgs(),
                                  fullPrompt, promptPath);
    m_process->setWorkingDirectory(m_tempDir->path());

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &HubSeoTranslator::_onProcessReadyRead);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &HubSeoTranslator::_onProcessFinished);

    m_process->start();
}

// =============================================================================
// Private: _onProcessFinished
// =============================================================================

void HubSeoTranslator::_onProcessFinished(int exitCode, QProcess::ExitStatus /*status*/)
{
    QString translatedOutput;
    bool hasError = false;

    if (m_process->error() == QProcess::FailedToStart) {
        _log(QStringLiteral("  %1 executable not found for hub SEO [%2] -> %3")
                 .arg(m_cli->getName(), m_currentJob.typeId, m_currentJob.targetLang), true);
        hasError = true;
    } else if (exitCode != 0) {
        const QString err = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
        _log(QStringLiteral("  %1 error for hub SEO [%2] -> %3: %4")
                 .arg(m_cli->getName(), m_currentJob.typeId, m_currentJob.targetLang,
                      err.isEmpty() ? QStringLiteral("exit code %1").arg(exitCode) : err),
             true);
        hasError = true;
    } else {
        m_processOutput += m_process->readAllStandardOutput();
        translatedOutput = m_cli->extractTextFromOutput(m_processOutput);
    }

    m_process->deleteLater();
    m_process = nullptr;
    m_processOutput.clear();
    m_tempDir.reset();

    if (hasError) {
        ++m_errors;
        _processNextJob();
        return;
    }

    const QHash<QString, QString> &translations = TranslationProtocol::parseResponse(translatedOutput);
    if (translations.isEmpty()) {
        _log(QStringLiteral("  Could not parse translation response for hub SEO [%1] -> %2")
                 .arg(m_currentJob.typeId, m_currentJob.targetLang), true);
        ++m_errors;
        _processNextJob();
        return;
    }

    int saved = 0;
    for (auto it = translations.cbegin(); it != translations.cend(); ++it) {
        if (it.value().isEmpty()) {
            continue;
        }
        m_db.set(m_currentJob.typeId, it.key(), m_currentJob.targetLang, it.value());
        ++saved;
    }

    _log(QStringLiteral("  Hub SEO [%1] -> %2: done (%3 template(s) saved)")
             .arg(m_currentJob.typeId, m_currentJob.targetLang)
             .arg(saved));
    ++m_translated;

    _processNextJob();
}

void HubSeoTranslator::_onProcessReadyRead()
{
    if (m_process) {
        m_processOutput += m_process->readAllStandardOutput();
    }
}

void HubSeoTranslator::_emitFinished(int translated, int errors)
{
    QMetaObject::invokeMethod(this, [this, translated, errors]() {
        emit finished(translated, errors);
    }, Qt::QueuedConnection);
}

void HubSeoTranslator::_log(const QString &msg, bool errorLevel)
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

void HubSeoTranslator::_openLogFile()
{
    const QDir logDir = QDir(m_workingDir.filePath(QStringLiteral("translation_logs")));
    if (!logDir.exists()) {
        m_workingDir.mkpath(QStringLiteral("translation_logs"));
    }

    const QString stamp    = QDateTime::currentDateTimeUtc()
                                 .toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString filePath = logDir.filePath(
        QStringLiteral("translate_hub_seo_%1.txt").arg(stamp));

    m_logFile = new QFile(filePath, this);
    if (!m_logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        qDebug() << "[TranslateHubSeo] Could not open log file:" << filePath;
        delete m_logFile;
        m_logFile = nullptr;
    } else {
        qDebug() << "[TranslateHubSeo] Log file:" << filePath;
    }
}
