#ifndef PAGETRANSLATOR_H
#define PAGETRANSLATOR_H

#include "website/WebCodeAdder.h"

#include <QDir>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>
#include <optional>

#include <QProcess>

class AbstractCli;
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
        int     pageId      = 0;   ///< source page id
        QString typeId;
        QString sourceLang;
        QString targetLang;
        QString svgFilename;       ///< non-empty → SVG sub-job (translate this image)
    };

    explicit PageTranslator(IPageRepository &repo,
                             CategoryTable   &categoryTable,
                             const QDir      &workingDir,
                             AbstractCli     *cli,
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
     * Scans all source pages for SVG images (via [IMGFIX *.svg] shortcodes in
     * 1_text) that do not yet have a translated blob in images.db and queues
     * SVG-only translation jobs for them.  Pages whose text translation is
     * already complete are included — this is the way to back-fill SVG
     * translations for articles translated before SVG translation was added.
     * Returns immediately; progress is reported via the same signals as start().
     */
    /**
     * languageFilter  when non-empty, only jobs for that target lang are queued.
     * limit           when >= 1, at most that many jobs are processed.
     */
    void startSvgJobs(const QString &editingLang,
                      const QString &languageFilter = {},
                      int            limit          = -1);

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
    // Saves a translated SVG blob to images.db under domain=lang.
    void _saveSvgTranslation(const QString    &filename,
                              const QString    &lang,
                              const QByteArray &svgData);
    // Rasterizes a translated SVG to one WebP per matching social-image size
    // and writes each under (lang, webpFilename) in images.db.
    void _rasterizeSvgToWebPs(const QString    &svgFilename,
                               const QByteArray &svgData,
                               const QString    &lang);

    // Splits text at paragraph (double-newline) boundaries, respecting shortcode
    // bracket balance.  Returns a single-element list if text <= maxChars.
    static QStringList _splitAtBlocks(const QString &text, int maxChars);

    // Writes the prompt to a temp file and launches the claude process.
    // Must be called with m_currentJob, m_currentPageType, m_currentPageData
    // all already populated.
    void _launchTextTranslation(const QList<TranslatableField> &fields);

    // Direct-mode chunk translation: sends a plain "translate this text" prompt
    // without the BEGIN/END protocol. Used for large intermediate chunks where
    // the model reliably ignores the structured format. The response is captured
    // as raw translated text. Sets m_isDirectChunkCall = true before launching.
    void _launchDirectChunkTranslation(const QString &chunkText);

    // Applies translations from a completed text job (or assembled chunks),
    // saves to the repository, queues SVG sub-jobs, and calls _processNextJob().
    void _finalizeTextTranslations(const QHash<QString, QString> &translations);

    IPageRepository &m_repo;
    CategoryTable   &m_categoryTable;
    QDir             m_workingDir;
    AbstractCli     *m_cli;
    AbstractEngine  *m_engine          = nullptr; ///< stored from start(); used for render validation
    QString          m_currentPermalink;           ///< permalink of the page currently being translated

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

    // Chunk-mode state — non-null while processing a job whose main field was
    // too large to translate in a single call and has been split into pieces.
    // Cleared (reset) when the chunked job succeeds or fails.
    struct ChunkState {
        QString  fieldId;          ///< id of the field being split
        int      chunkIndex = 0;   ///< index of the chunk just dispatched
        int      totalChunks = 0;  ///< total number of chunks
        QStringList pendingChunks; ///< chunks not yet dispatched (front = next)
        QString  assembledText;    ///< translated content accumulated so far
        QHash<QString, QString> otherTranslations; ///< non-chunked field results
        QList<TranslatableField> otherFields; ///< non-chunked fields appended to last chunk call
    };
    std::optional<ChunkState> m_chunkState;

    // When true, the in-flight claude call is a direct chunk (no BEGIN/END protocol).
    // _onProcessFinished uses the raw response text as the chunk translation directly.
    bool m_isDirectChunkCall = false;

    /// Fields per chunk; >28 k chars in a single field triggers chunking.
    static constexpr int MAX_CHUNK_CHARS = 28'000;

    /// Minimum source length before the translation-length ratio check is applied.
    static constexpr int    MIN_LENGTH_CHECK_CHARS  = 1'000;
    /// Translation must be at least this fraction of source length (CJK is ~40-60 %).
    static constexpr double MIN_TRANSLATION_RATIO   = 0.35;

    QFile *m_logFile = nullptr;
};

#endif // PAGETRANSLATOR_H
