#ifndef TAXONOMYTRANSLATOR_H
#define TAXONOMYTRANSLATOR_H

#include "website/WebCodeAdder.h"

#include <QDir>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <QProcess>

class AbstractCli;
class QFile;
class QTemporaryDir;
class TaxonomyDb;

/**
 * Translates taxonomy vocabulary items (e.g. symptom names) to every target
 * language for which translations are currently missing.
 *
 * One job is created per (type, targetLang) pair that has untranslated items.
 * All untranslated items for that pair are batched into a single prompt so the
 * entire vocabulary is translated in one API call.
 *
 * Field ids are sequential integers (as strings) mapped back to English names
 * via an internal list, so names with special characters never corrupt the
 * TranslationProtocol delimiter format.
 *
 * Integrated into the --translateCommon pipeline via LauncherTranslateCommon.
 */
class TaxonomyTranslator : public QObject
{
    Q_OBJECT

public:
    struct TranslationJob {
        QString                  type;
        QString                  targetLang;
        QStringList              englishNames; ///< items to translate; index == field id
        QList<TranslatableField> fields;        ///< fields[i].id == QString::number(i)
    };

    explicit TaxonomyTranslator(TaxonomyDb  &taxonomyDb,
                                 const QDir  &workingDir,
                                 AbstractCli *cli,
                                 QObject     *parent = nullptr);
    ~TaxonomyTranslator() override;

    /**
     * Returns one job per (type, targetLang) pair that has at least one item
     * without a translation.  Pairs where targetLang == sourceLang are skipped.
     */
    static QList<TranslationJob> buildJobs(TaxonomyDb        &taxonomyDb,
                                            const QStringList &types,
                                            const QString     &sourceLang,
                                            const QStringList &targetLangs);

    /** Starts async translation of the supplied job list. */
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

    TaxonomyDb               &m_taxonomyDb;
    QDir                      m_workingDir;
    AbstractCli              *m_cli;

    QProcess                 *m_process   = nullptr;
    QByteArray                m_processOutput;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    QList<TranslationJob>     m_queue;
    TranslationJob            m_currentJob;
    int                       m_translated = 0;
    int                       m_errors     = 0;

    QFile                    *m_logFile    = nullptr;
};

#endif // TAXONOMYTRANSLATOR_H
