#include "CategoryTranslator.h"

#include "aicli/AbstractCli.h"

#include "TranslationProtocol.h"
#include "website/pages/attributes/CategoryTable.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>

// =============================================================================
// Constructor / Destructor
// =============================================================================

CategoryTranslator::CategoryTranslator(CategoryTable &categoryTable,
                                         const QDir    &workingDir,
                                         AbstractCli   *cli,
                                         QObject       *parent)
    : QObject(parent)
    , m_categoryTable(categoryTable)
    , m_workingDir(workingDir)
    , m_cli(cli)
{
}

CategoryTranslator::~CategoryTranslator()
{
    if (m_logFile && m_logFile->isOpen()) {
        m_logFile->close();
    }
    delete m_logFile;
}

// =============================================================================
// buildJobs (static)
// =============================================================================

QList<CategoryTranslator::TranslationJob>
CategoryTranslator::buildJobs(const CategoryTable &table,
                                const QString       &sourceLang,
                                const QStringList   &targetLangs)
{
    QList<TranslationJob> jobs;

    for (const QString &targetLang : std::as_const(targetLangs)) {
        if (targetLang == sourceLang) {
            continue;
        }

        QList<TranslatableField> fields;
        for (const CategoryTable::CategoryRow &row : std::as_const(table.categories())) {
            if (row.name.isEmpty()) {
                continue;
            }
            if (!row.translations.contains(targetLang)) {
                TranslatableField f;
                f.id         = QString::number(row.id);
                f.sourceText = row.name;
                fields.append(f);
            }
        }

        if (!fields.isEmpty()) {
            TranslationJob job;
            job.targetLang = targetLang;
            job.fields     = std::move(fields);
            jobs.append(job);
        }
    }

    return jobs;
}

// =============================================================================
// startWithJobs
// =============================================================================

void CategoryTranslator::startWithJobs(const QList<TranslationJob> &jobs)
{
    _openLogFile();

    m_queue = jobs;
    _log(QStringLiteral("Category translation started. %1 job(s) queued.")
             .arg(m_queue.size()));

    if (m_queue.isEmpty()) {
        _log(QStringLiteral("Nothing to translate. All category names are up to date."));
        _emitFinished(0, 0);
        return;
    }

    _processNextJob();
}

// =============================================================================
// Private: _processNextJob
// =============================================================================

void CategoryTranslator::_processNextJob()
{
    if (m_queue.isEmpty()) {
        _log(QStringLiteral("All jobs done. Translated: %1  Errors: %2")
                 .arg(m_translated).arg(m_errors));
        _emitFinished(m_translated, m_errors);
        return;
    }

    m_currentJob = m_queue.takeFirst();

    _log(QStringLiteral("Processing %1 category name(s) → %2 …")
             .arg(m_currentJob.fields.size())
             .arg(m_currentJob.targetLang));

    // Source lang is implicit: the field sourceText is already the English name.
    // We use "en" as placeholder — TranslationProtocol only uses it for the prompt header.
    const QString prompt = TranslationProtocol::buildPrompt(
        m_currentJob.fields, QStringLiteral("en"), m_currentJob.targetLang);

    m_tempDir = std::make_unique<QTemporaryDir>();
    if (!m_tempDir->isValid()) {
        _log(QStringLiteral("  Failed to create temp dir for categories → %1")
                 .arg(m_currentJob.targetLang), true);
        ++m_errors;
        m_tempDir.reset();
        _processNextJob();
        return;
    }

    const QString promptPath = m_tempDir->path() + QStringLiteral("/prompt.txt");
    {
        QFile f(promptPath);
        if (!f.open(QIODevice::WriteOnly)) {
            _log(QStringLiteral("  Failed to write prompt file for categories → %1")
                     .arg(m_currentJob.targetLang), true);
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
    m_process->setArguments({QStringLiteral("-p"), QStringLiteral("-"),
                              QStringLiteral("--dangerously-skip-permissions"),
                              QStringLiteral("--tools"), QStringLiteral(""),
                              QStringLiteral("--output-format"), QStringLiteral("stream-json"),
                              QStringLiteral("--verbose")});
    m_process->setStandardInputFile(promptPath);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &CategoryTranslator::_onProcessReadyRead);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &CategoryTranslator::_onProcessFinished);

    m_process->start();
}

// =============================================================================
// Private: _onProcessFinished
// =============================================================================

void CategoryTranslator::_onProcessFinished(int exitCode, QProcess::ExitStatus /*status*/)
{
    QString translatedJson;
    bool hasError = false;

    if (m_process->error() == QProcess::FailedToStart) {
        _log(QStringLiteral("  claude executable not found in PATH for categories → %1")
                 .arg(m_currentJob.targetLang), true);
        hasError = true;
    } else if (exitCode != 0) {
        const QString err = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
        _log(QStringLiteral("  claude error for categories → %1: %2")
                 .arg(m_currentJob.targetLang,
                      err.isEmpty() ? QStringLiteral("exit code %1").arg(exitCode) : err),
             true);
        hasError = true;
    } else {
        m_processOutput += m_process->readAllStandardOutput();

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
            if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("result")) {
                translatedJson = obj.value(QStringLiteral("result")).toString().trimmed();
                break;
            }
        }
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

    const QHash<QString, QString> &translations = TranslationProtocol::parseResponse(translatedJson);
    if (translations.isEmpty()) {
        _log(QStringLiteral("  Could not parse translation response for categories → %1")
                 .arg(m_currentJob.targetLang), true);
        ++m_errors;
        _processNextJob();
        return;
    }

    int saved = 0;
    for (auto it = translations.cbegin(); it != translations.cend(); ++it) {
        bool ok = false;
        const int catId = it.key().toInt(&ok);
        if (!ok || it.value().isEmpty()) {
            continue;
        }
        m_categoryTable.setTranslation(catId, m_currentJob.targetLang, it.value());
        ++saved;
    }

    _log(QStringLiteral("  Categories → %1: done (%2 name(s) saved)")
             .arg(m_currentJob.targetLang).arg(saved));
    ++m_translated;

    _processNextJob();
}

void CategoryTranslator::_onProcessReadyRead()
{
    if (m_process) {
        m_processOutput += m_process->readAllStandardOutput();
    }
}

void CategoryTranslator::_emitFinished(int translated, int errors)
{
    QMetaObject::invokeMethod(this, [this, translated, errors]() {
        emit finished(translated, errors);
    }, Qt::QueuedConnection);
}

void CategoryTranslator::_log(const QString &msg, bool errorLevel)
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

void CategoryTranslator::_openLogFile()
{
    const QDir logDir = QDir(m_workingDir.filePath(QStringLiteral("translation_logs")));
    if (!logDir.exists()) {
        m_workingDir.mkpath(QStringLiteral("translation_logs"));
    }

    const QString stamp    = QDateTime::currentDateTimeUtc()
                                 .toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString filePath = logDir.filePath(
        QStringLiteral("translate_categories_%1.txt").arg(stamp));

    m_logFile = new QFile(filePath, this);
    if (!m_logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        qDebug() << "[TranslateCategories] Could not open log file:" << filePath;
        delete m_logFile;
        m_logFile = nullptr;
    } else {
        qDebug() << "[TranslateCategories] Log file:" << filePath;
    }
}
