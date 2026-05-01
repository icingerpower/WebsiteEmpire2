#ifndef PAGETRANSLATOR_H
#define PAGETRANSLATOR_H

#include "website/WebCodeAdder.h"

#include <QDir>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

#include <QProcess>

class AbstractEngine;
class AbstractPageType;
class CategoryTable;
class IPageRepository;
class QFile;
class QTemporaryDir;

/**
 * Translates source pages to all target languages declared by their
 * langCodesToTranslate field.
 *
 * Workflow
 * --------
 * 1. start() scans IPageRepository for source pages whose lang == editingLang.
 * 2. For each page, iterates page.langCodesToTranslate.  A job is queued for
 *    every (page × targetLang) pair.
 * 3. Jobs are dispatched to the claude CLI one at a time via QProcess.
 *    In _processNextJob(), the page type is loaded and
 *    isTranslationComplete() is checked; already-complete pairs are skipped
 *    without a process call.
 *    collectTranslatables() gathers the fields that still need work; the AI
 *    receives a JSON list of {id, source} objects and must return a JSON map
 *    {fieldId: translatedText}.
 * 4. applyTranslation() stores each returned value inside the in-memory page
 *    type; save() + saveData() persist everything (original data + new
 *    translations) back to the repository in one atomic write.
 * 5. signals logMessage(), pageTranslated(), and finished() keep the caller
 *    informed throughout.
 *
 * Translation data is stored inline in the page's own data map under
 * "tr:<lang>:<fieldId>" keys, managed by BlocTranslations.  No separate
 * translation page records are created.
 *
 * Log output
 * ----------
 * Every log line is emitted via logMessage() and simultaneously appended to a
 * timestamped file under <workingDir>/translation_logs/.
 *
 * Transport
 * ---------
 * Uses the claude CLI (must be on PATH) via QProcess with
 * --dangerously-skip-permissions --tools "" --output-format stream-json --verbose.
 *
 * --tools "" prevents Claude from using file-writing tools that would cause
 * claude -p to emit only the last AI turn rather than the full translation.
 *
 * --output-format stream-json --verbose makes the CLI emit a JSON object per
 * line and include a final {"type":"result","result":"..."} line that holds
 * the complete compiled response, avoiding the stdout truncation that affects
 * the default text format for large (>~10 KB) responses.
 *
 * No ANTHROPIC_API_KEY is required; the Claude Code session credentials are
 * used instead.
 *
 * The class holds non-owning references to IPageRepository and CategoryTable;
 * both must outlive this object.
 */
class PageTranslator : public QObject
{
    Q_OBJECT

public:
    struct TranslationJob {
        int     pageId     = 0;   ///< source page id
        QString typeId;
        QString sourceLang;
        QString targetLang;
    };

    explicit PageTranslator(IPageRepository &repo,
                             CategoryTable   &categoryTable,
                             const QDir      &workingDir,
                             QObject         *parent = nullptr);
    ~PageTranslator() override;

    /**
     * Starts async translation.  Returns immediately; progress is reported
     * via signals.  engine provides the available lang codes; editingLang is
     * the source lang.
     */
    void start(AbstractEngine *engine, const QString &editingLang);

    /**
     * Starts async translation from a pre-built job list produced by
     * TranslationScheduler::buildJobs().  Bypasses the internal
     * _buildJobQueue() scan — useful when the caller has already applied
     * assessment and priority ordering.
     * Returns immediately; progress is reported via the same signals as start().
     */
    void startWithJobs(const QList<TranslationJob> &jobs);

    /**
     * Returns all pending translation jobs as a human-readable string of
     * Claude prompts — one block per job — suitable for manual submission.
     * Does not launch any process.
     */
    QString buildPrompts(AbstractEngine *engine, const QString &editingLang);

signals:
    void logMessage(const QString &msg);              ///< progress / info
    void pageTranslated(int pageId);                  ///< after each success
    void finished(int translated, int errors);        ///< all work complete

private slots:
    void _onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void _onProcessReadyRead();

private:
    QList<TranslationJob> _buildJobQueue(AbstractEngine *engine,
                                          const QString  &editingLang);
    void _processNextJob();
    void _log(const QString &msg, bool errorLevel = false);
    void _openLogFile();
    // Always deferred via Qt::QueuedConnection so quit() fires after exec() starts.
    void _emitFinished(int translated, int errors);

    IPageRepository &m_repo;
    CategoryTable   &m_categoryTable;
    QDir             m_workingDir;

    QProcess                  *m_process   = nullptr;
    QByteArray                 m_processOutput; // accumulated stdout from running process
    std::unique_ptr<QTemporaryDir> m_tempDir;
    QList<TranslationJob>      m_queue;
    TranslationJob             m_currentJob;
    int                        m_translated = 0;
    int                        m_errors     = 0;

    // State kept alive across the async process call.
    std::unique_ptr<AbstractPageType> m_currentPageType;
    QHash<QString, QString>           m_currentPageData;
    QString                           m_currentSingleFieldId; ///< set iff the current job has exactly one field

    QFile *m_logFile = nullptr;
};

#endif // PAGETRANSLATOR_H
