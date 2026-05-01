#ifndef COMMONBLOCTRANSLATOR_H
#define COMMONBLOCTRANSLATOR_H

#include "website/WebCodeAdder.h"

#include <QDir>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

#include <QProcess>

class AbstractCommonBloc;
class AbstractTheme;
class QFile;
class QTemporaryDir;

/**
 * Translates common bloc fields (header, footer, cookies, …) to every target
 * language for which translations are currently missing.
 *
 * Workflow
 * --------
 * 1. buildJobs() scans a list of blocs and produces one job per
 *    (bloc × targetLang) pair that has at least one untranslated field.
 *    Pairs where targetLang == sourceLang are skipped.
 * 2. startWithJobs() dispatches those jobs to the claude CLI one at a time.
 *    For each job it:
 *      a) Finds the bloc by ID in the theme.
 *      b) Collects source texts via sourceTexts() and filters to fields
 *         that still lack a translation for targetLang.
 *      c) Sends a JSON prompt with the same structure as PageTranslator.
 *      d) Parses the response and stores each translation via setTranslation().
 *      e) Calls AbstractTheme::saveBlocsData() to persist immediately.
 * 3. Signals logMessage(), blocTranslated(), and finished() keep callers
 *    informed throughout.
 *
 * Transport
 * ---------
 * Uses the claude CLI (must be on PATH) via QProcess with
 * --dangerously-skip-permissions --tools "".
 * --tools "" disables all tool use so Claude returns the translation inline.
 * No ANTHROPIC_API_KEY is required.
 *
 * The class holds a non-owning reference to AbstractTheme; the theme must
 * outlive this object.
 */
class CommonBlocTranslator : public QObject
{
    Q_OBJECT

public:
    struct TranslationJob {
        QString blocId;      ///< stable ID matching AbstractCommonBloc::getId()
        QString sourceLang;
        QString targetLang;
    };

    explicit CommonBlocTranslator(AbstractTheme &theme,
                                   const QDir    &workingDir,
                                   QObject       *parent = nullptr);
    ~CommonBlocTranslator() override;

    /**
     * Returns one job per (bloc × targetLang) pair that has at least one
     * field returned by missingTranslations().  Pairs where
     * targetLang == sourceLang are skipped.
     */
    static QList<TranslationJob> buildJobs(const QList<AbstractCommonBloc *> &blocs,
                                            const QString                     &sourceLang,
                                            const QStringList                 &targetLangs);

    /**
     * Starts async translation of the supplied job list.  Returns immediately;
     * progress is reported via logMessage(), blocTranslated(), and finished().
     */
    void startWithJobs(const QList<TranslationJob> &jobs);

signals:
    void logMessage(const QString &msg);
    void blocTranslated(const QString &blocId, const QString &targetLang);
    void finished(int translated, int errors);

private slots:
    void _onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void _onProcessReadyRead();

private:
    void    _processNextJob();
    void    _log(const QString &msg, bool errorLevel = false);
    void    _openLogFile();
    void    _emitFinished(int translated, int errors);

    AbstractTheme            &m_theme;
    QDir                      m_workingDir;

    QProcess                 *m_process     = nullptr;
    QByteArray                m_processOutput;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    QList<TranslationJob>     m_queue;
    TranslationJob            m_currentJob;
    AbstractCommonBloc       *m_currentBloc = nullptr;
    int                       m_translated  = 0;
    int                       m_errors      = 0;

    QFile                    *m_logFile     = nullptr;
};

#endif // COMMONBLOCTRANSLATOR_H
