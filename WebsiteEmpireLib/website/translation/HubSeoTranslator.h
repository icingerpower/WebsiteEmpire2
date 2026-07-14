#ifndef HUBSEOTRANSLATOR_H
#define HUBSEOTRANSLATOR_H

#include "website/pages/HubSeoTemplateDb.h"
#include "website/WebCodeAdder.h"

#include <QDir>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <QProcess>

class AbstractCli;
class QFile;
class QTemporaryDir;

/**
 * Translates hub-page SEO template strings (title/description patterns with
 * %1/%2 Qt-style placeholders) to every target language for which translations
 * are currently missing.
 *
 * Template strings use %1/%2 placeholders (e.g. "What Causes %1? %2 Conditions
 * To Know").  Claude is instructed to preserve all %N placeholders verbatim.
 *
 * One job is created per (typeId × targetLang) pair that has at least one
 * untranslated template key.  All untranslated keys for that pair are batched
 * into a single prompt so the entire set is translated in one API call.
 *
 * Translations are persisted to hub_seo.db via HubSeoTemplateDb and consumed
 * by AbstractPageType::seoTemplate() at render time.
 *
 * Integrated into the --translateCommon pipeline via LauncherTranslateCommon.
 */
class HubSeoTranslator : public QObject
{
    Q_OBJECT

public:
    struct TranslationJob {
        QString                  typeId;
        QString                  targetLang;
        QList<TranslatableField> fields; ///< one field per untranslated template key
    };

    explicit HubSeoTranslator(const QDir  &workingDir,
                               AbstractCli *cli,
                               QObject     *parent = nullptr);
    ~HubSeoTranslator() override;

    /**
     * Returns one job per (typeId × targetLang) pair that has at least one
     * template key without a stored translation.
     * Pairs where targetLang == sourceLang are skipped.
     * Already-translated (typeId, key, langCode) triples are skipped via m_db.
     */
    QList<TranslationJob> buildJobs(
        const QMap<QString, QMap<QString, QString>> &pageTypeTemplates,
        const QString                               &sourceLang,
        const QStringList                           &targetLangs) const;

    /**
     * Starts async translation of the supplied job list.  Returns immediately;
     * progress is reported via logMessage() and finished().
     */
    void startWithJobs(const QList<TranslationJob> &jobs);

signals:
    void logMessage(const QString &msg);
    void finished(int translated, int errors);

private slots:
    void _onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void _onProcessReadyRead();

private:
    void _processNextJob();
    void _log(const QString &msg, bool errorLevel = false);
    void _openLogFile();
    void _emitFinished(int translated, int errors);

    HubSeoTemplateDb          m_db;
    QDir                      m_workingDir;
    AbstractCli              *m_cli;

    QProcess                 *m_process    = nullptr;
    QByteArray                m_processOutput;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    QList<TranslationJob>     m_queue;
    TranslationJob            m_currentJob;
    int                       m_translated = 0;
    int                       m_errors     = 0;

    QFile                    *m_logFile    = nullptr;
};

#endif // HUBSEOTRANSLATOR_H
