#include "TaxonomyTranslator.h"

#include "aicli/AbstractCli.h"
#include "website/taxonomy/TaxonomyDb.h"
#include "website/translation/TranslationProtocol.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>

// =============================================================================
// Constructor / Destructor
// =============================================================================

TaxonomyTranslator::TaxonomyTranslator(TaxonomyDb  &taxonomyDb,
                                         const QDir  &workingDir,
                                         AbstractCli *cli,
                                         QObject     *parent)
    : QObject(parent)
    , m_taxonomyDb(taxonomyDb)
    , m_workingDir(workingDir)
    , m_cli(cli)
{
}

TaxonomyTranslator::~TaxonomyTranslator()
{
    if (m_logFile && m_logFile->isOpen()) {
        m_logFile->close();
    }
    delete m_logFile;
}

// =============================================================================
// buildJobs (static)
// =============================================================================

QList<TaxonomyTranslator::TranslationJob>
TaxonomyTranslator::buildJobs(TaxonomyDb        &taxonomyDb,
                                const QStringList &types,
                                const QString     &sourceLang,
                                const QStringList &targetLangs)
{
    QList<TranslationJob> jobs;

    for (const QString &type : std::as_const(types)) {
        for (const QString &targetLang : std::as_const(targetLangs)) {
            if (targetLang == sourceLang) {
                continue;
            }

            const QStringList untranslated = taxonomyDb.loadUntranslated(type, targetLang);
            if (untranslated.isEmpty()) {
                continue;
            }

            TranslationJob job;
            job.type       = type;
            job.targetLang = targetLang;
            job.englishNames = untranslated;

            job.fields.reserve(untranslated.size());
            for (int i = 0; i < untranslated.size(); ++i) {
                TranslatableField f;
                f.id         = QString::number(i);
                f.sourceText = untranslated.at(i);
                job.fields.append(std::move(f));
            }

            jobs.append(std::move(job));
        }
    }

    return jobs;
}

// =============================================================================
// startWithJobs
// =============================================================================

void TaxonomyTranslator::startWithJobs(const QList<TranslationJob> &jobs)
{
    _openLogFile();

    m_queue = jobs;
    _log(QStringLiteral("Taxonomy translation started. %1 job(s) queued.")
             .arg(m_queue.size()));

    if (m_queue.isEmpty()) {
        _log(QStringLiteral("Nothing to translate. All taxonomy names are up to date."));
        _emitFinished(0, 0);
        return;
    }

    _processNextJob();
}

// =============================================================================
// Private: _processNextJob
// =============================================================================

void TaxonomyTranslator::_processNextJob()
{
    if (m_queue.isEmpty()) {
        _log(QStringLiteral("All jobs done. Translated: %1  Errors: %2")
                 .arg(m_translated).arg(m_errors));
        _emitFinished(m_translated, m_errors);
        return;
    }

    m_currentJob = m_queue.takeFirst();

    _log(QStringLiteral("Processing %1 '%2' name(s) → %3 …")
             .arg(m_currentJob.fields.size())
             .arg(m_currentJob.type, m_currentJob.targetLang));

    const QString prompt = TranslationProtocol::buildPrompt(
        m_currentJob.fields, QStringLiteral("en"), m_currentJob.targetLang);

    m_tempDir = std::make_unique<QTemporaryDir>();
    if (!m_tempDir->isValid()) {
        _log(QStringLiteral("  Failed to create temp dir for '%1' → %2")
                 .arg(m_currentJob.type, m_currentJob.targetLang), true);
        ++m_errors;
        m_tempDir.reset();
        _processNextJob();
        return;
    }

    const QString promptPath = m_tempDir->path() + QStringLiteral("/prompt.txt");
    {
        QFile f(promptPath);
        if (!f.open(QIODevice::WriteOnly)) {
            _log(QStringLiteral("  Failed to write prompt for '%1' → %2")
                     .arg(m_currentJob.type, m_currentJob.targetLang), true);
            ++m_errors;
            m_tempDir.reset();
            _processNextJob();
            return;
        }
        f.write(prompt.toUtf8());
    }

    m_processOutput.clear();

    m_process = new QProcess(this);
    m_process->setProgram(m_cli->getExecutable());
    m_process->setArguments(m_cli->translationPromptArgs());
    m_process->setStandardInputFile(promptPath);
    m_process->setWorkingDirectory(m_tempDir->path());

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &TaxonomyTranslator::_onProcessReadyRead);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TaxonomyTranslator::_onProcessFinished);

    m_process->start();
}

// =============================================================================
// Private: _onProcessFinished
// =============================================================================

void TaxonomyTranslator::_onProcessFinished(int exitCode, QProcess::ExitStatus /*status*/)
{
    QString translatedText;
    bool hasError = false;

    if (m_process->error() == QProcess::FailedToStart) {
        _log(QStringLiteral("  %1 executable not found for '%2' → %3")
                 .arg(m_cli->getName(), m_currentJob.type, m_currentJob.targetLang), true);
        hasError = true;
    } else if (exitCode != 0) {
        const QString err = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
        QString detail = err;
        if (detail.isEmpty()) {
            const QString outPath = m_tempDir ? m_tempDir->path() + QStringLiteral("/output") : QString();
            QFile f(outPath);
            if (f.open(QIODevice::ReadOnly)) {
                detail = QString::fromUtf8(f.readAll()).trimmed().left(300);
            }
        }
        if (detail.isEmpty()) {
            detail = QStringLiteral("exit code %1").arg(exitCode);
        }
        _log(QStringLiteral("  %1 error for '%2' → %3: %4")
                 .arg(m_cli->getName(), m_currentJob.type, m_currentJob.targetLang, detail), true);
        hasError = true;
    } else {
        m_processOutput += m_process->readAllStandardOutput();
        translatedText = m_cli->extractTextFromOutput(m_processOutput);
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

    const QHash<QString, QString> &translations = TranslationProtocol::parseResponse(translatedText);
    if (translations.isEmpty()) {
        _log(QStringLiteral("  Could not parse translation response for '%1' → %2")
                 .arg(m_currentJob.type, m_currentJob.targetLang), true);
        ++m_errors;
        _processNextJob();
        return;
    }

    int saved = 0;
    for (auto it = translations.cbegin(); it != translations.cend(); ++it) {
        bool ok = false;
        const int idx = it.key().toInt(&ok);
        if (!ok || idx < 0 || idx >= m_currentJob.englishNames.size()) {
            continue;
        }
        if (it.value().isEmpty()) {
            continue;
        }
        m_taxonomyDb.setTranslation(m_currentJob.type,
                                     m_currentJob.englishNames.at(idx),
                                     m_currentJob.targetLang,
                                     it.value());
        ++saved;
    }

    _log(QStringLiteral("  '%1' → %2: done (%3 name(s) saved)")
             .arg(m_currentJob.type, m_currentJob.targetLang).arg(saved));
    ++m_translated;

    _processNextJob();
}

void TaxonomyTranslator::_onProcessReadyRead()
{
    if (m_process) {
        m_processOutput += m_process->readAllStandardOutput();
    }
}

void TaxonomyTranslator::_emitFinished(int translated, int errors)
{
    QMetaObject::invokeMethod(this, [this, translated, errors]() {
        emit finished(translated, errors);
    }, Qt::QueuedConnection);
}

void TaxonomyTranslator::_log(const QString &msg, bool errorLevel)
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

void TaxonomyTranslator::_openLogFile()
{
    const QDir logDir = QDir(m_workingDir.filePath(QStringLiteral("translation_logs")));
    if (!logDir.exists()) {
        m_workingDir.mkpath(QStringLiteral("translation_logs"));
    }

    const QString stamp    = QDateTime::currentDateTimeUtc()
                                 .toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString filePath = logDir.filePath(
        QStringLiteral("translate_taxonomy_%1.txt").arg(stamp));

    m_logFile = new QFile(filePath, this);
    if (!m_logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        qDebug() << "[TranslateTaxonomy] Could not open log file:" << filePath;
        delete m_logFile;
        m_logFile = nullptr;
    } else {
        qDebug() << "[TranslateTaxonomy] Log file:" << filePath;
    }
}
