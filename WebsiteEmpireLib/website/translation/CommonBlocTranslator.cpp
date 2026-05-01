#include "CommonBlocTranslator.h"

#include "TranslationProtocol.h"
#include "website/commonblocs/AbstractCommonBloc.h"
#include "website/theme/AbstractTheme.h"

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

CommonBlocTranslator::CommonBlocTranslator(AbstractTheme &theme,
                                             const QDir    &workingDir,
                                             QObject       *parent)
    : QObject(parent)
    , m_theme(theme)
    , m_workingDir(workingDir)
{
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

    m_queue = jobs;
    _log(QStringLiteral("Common bloc translation started. %1 job(s) queued.")
             .arg(m_queue.size()));

    if (m_queue.isEmpty()) {
        _log(QStringLiteral("Nothing to translate. All common bloc fields are up to date."));
        _emitFinished(0, 0);
        return;
    }

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
        _emitFinished(m_translated, m_errors);
        return;
    }

    m_currentJob  = m_queue.takeFirst();
    m_currentBloc = nullptr;

    _log(QStringLiteral("Processing bloc '%1' → %2 …")
             .arg(m_currentJob.blocId, m_currentJob.targetLang));

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

    const QHash<QString, QString> &sources = m_currentBloc->sourceTexts();
    QList<TranslatableField> fields;
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        if (it.value().isEmpty()) {
            continue;
        }
        if (!m_currentBloc->translatedText(it.key(), m_currentJob.targetLang).isEmpty()) {
            continue;
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

    const QString prompt = TranslationProtocol::buildPrompt(fields, m_currentJob.sourceLang, m_currentJob.targetLang);

    // Write prompt to a temp file and launch the claude CLI.
    m_tempDir = std::make_unique<QTemporaryDir>();
    if (!m_tempDir->isValid()) {
        _log(QStringLiteral("  Failed to create temp dir for bloc '%1' → %2")
                 .arg(m_currentJob.blocId, m_currentJob.targetLang), true);
        ++m_errors;
        m_tempDir.reset();
        _processNextJob();
        return;
    }

    const QString promptPath = m_tempDir->path() + QStringLiteral("/prompt.txt");
    {
        QFile f(promptPath);
        if (!f.open(QIODevice::WriteOnly)) {
            _log(QStringLiteral("  Failed to write prompt file for bloc '%1' → %2")
                     .arg(m_currentJob.blocId, m_currentJob.targetLang), true);
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
            this, &CommonBlocTranslator::_onProcessReadyRead);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &CommonBlocTranslator::_onProcessFinished);

    m_process->start();
}

// =============================================================================
// Private: _onProcessFinished
// =============================================================================

void CommonBlocTranslator::_onProcessFinished(int exitCode, QProcess::ExitStatus /*status*/)
{
    QString translatedJson;
    bool hasError = false;

    if (m_process->error() == QProcess::FailedToStart) {
        _log(QStringLiteral("  claude executable not found in PATH for bloc '%1' → %2")
                 .arg(m_currentJob.blocId, m_currentJob.targetLang), true);
        hasError = true;
    } else if (exitCode != 0) {
        const QString err = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
        _log(QStringLiteral("  claude error for bloc '%1' → %2: %3")
                 .arg(m_currentJob.blocId, m_currentJob.targetLang,
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
        _log(QStringLiteral("  Could not parse translation response for bloc '%1' → %2")
                 .arg(m_currentJob.blocId, m_currentJob.targetLang), true);
        ++m_errors;
        _processNextJob();
        return;
    }

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

void CommonBlocTranslator::_onProcessReadyRead()
{
    if (m_process) {
        m_processOutput += m_process->readAllStandardOutput();
    }
}

void CommonBlocTranslator::_emitFinished(int translated, int errors)
{
    QMetaObject::invokeMethod(this, [this, translated, errors]() {
        emit finished(translated, errors);
    }, Qt::QueuedConnection);
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
