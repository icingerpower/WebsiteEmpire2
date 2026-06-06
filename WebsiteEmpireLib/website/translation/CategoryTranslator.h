#ifndef CATEGORYTRANSLATOR_H
#define CATEGORYTRANSLATOR_H

#include "website/WebCodeAdder.h"

#include <QDir>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

#include <QProcess>

class AbstractCli;
class CategoryTable;
class QFile;
class QTemporaryDir;

/**
 * Translates category names (categories.csv) to every target language for
 * which translations are currently missing.
 *
 * One job is created per target language.  All untranslated categories for
 * that language are batched into a single Claude prompt so the entire
 * vocabulary is translated in one API call per language.
 *
 * Field ids in the prompt are the string representations of the integer
 * category ids; the response is applied via CategoryTable::setTranslation().
 *
 * Integrated into the --translateCommon pipeline so running that launcher
 * also keeps category names up to date.
 */
class CategoryTranslator : public QObject
{
    Q_OBJECT

public:
    struct TranslationJob {
        QString                 targetLang;
        QList<TranslatableField> fields; ///< one field per untranslated category
    };

    explicit CategoryTranslator(CategoryTable &categoryTable,
                                 const QDir    &workingDir,
                                 AbstractCli   *cli,
                                 QObject       *parent = nullptr);
    ~CategoryTranslator() override;

    /**
     * Returns one job per target language that has at least one category
     * without a translation.  Pairs where targetLang == sourceLang are skipped.
     */
    static QList<TranslationJob> buildJobs(const CategoryTable &table,
                                            const QString       &sourceLang,
                                            const QStringList   &targetLangs);

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

    CategoryTable            &m_categoryTable;
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

#endif // CATEGORYTRANSLATOR_H
