#ifndef PAGETRANSLATOR_H
#define PAGETRANSLATOR_H

#include <QDir>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

class AbstractEngine;
class CategoryTable;
class IPageRepository;
class QNetworkAccessManager;
class QNetworkReply;

/**
 * Translates source pages to all target languages declared by an AbstractEngine.
 *
 * Workflow
 * --------
 * 1. start() scans IPageRepository for source pages (sourcePageId == 0,
 *    lang == editingLang) and all target langs from the engine.
 * 2. For each (sourcePage × targetLang) that needs work — no existing
 *    translation or source updated after last translatedAt — a TranslationJob
 *    is queued.
 * 3. Jobs are dispatched to the Claude API one at a time.  Each response is
 *    parsed, saved via IPageRepository, and stamped with setTranslatedAt().
 * 4. signals logMessage(), pageTranslated(), and finished() keep the caller
 *    informed throughout.
 *
 * Log output
 * ----------
 * Every log line is emitted via logMessage() (connected to qDebug() by the
 * caller) and simultaneously appended to a timestamped file under
 * <workingDir>/translation_logs/.  Unexpected errors are written there with
 * full detail even when the per-line debug output is concise.
 *
 * API key
 * -------
 * Read from the ANTHROPIC_API_KEY environment variable.
 *
 * The class holds non-owning references to IPageRepository and CategoryTable;
 * both must outlive this object.
 */
class PageTranslator : public QObject
{
    Q_OBJECT

public:
    struct TranslationJob {
        int     sourcePageId    = 0;
        int     targetPageId    = 0; // 0 = create new translation page
        QString typeId;
        QString sourceLang;
        QString targetLang;
        QString targetPermalink; // pre-computed placeholder permalink
    };

    explicit PageTranslator(IPageRepository &repo,
                             CategoryTable   &categoryTable,
                             const QDir      &workingDir,
                             QObject         *parent = nullptr);
    ~PageTranslator() override;

    /**
     * Starts async translation.  Returns immediately; progress is reported
     * via signals.  A QNetworkAccessManager is created internally.
     * engine and editingLang must match how the source pages were created.
     */
    void start(AbstractEngine *engine, const QString &editingLang);

    /**
     * Returns all pending translation jobs as a human-readable string of
     * Claude API prompts — one block per job — suitable for manual submission
     * or copy-pasting into a conversation.  Does not make any network calls.
     */
    QString buildPrompts(AbstractEngine *engine, const QString &editingLang);

signals:
    void logMessage(const QString &msg);              ///< progress / info
    void pageTranslated(int pageId);                  ///< after each success
    void finished(int translated, int errors);        ///< all work complete

private slots:
    void _onReplyFinished();

private:
    QList<TranslationJob> _buildJobQueue(AbstractEngine *engine,
                                          const QString  &editingLang);
    void _processNextJob();
    QString _buildPrompt(const QHash<QString, QString> &data,
                          const QString &sourceLang,
                          const QString &targetLang) const;
    QHash<QString, QString> _parseResponse(const QString &jsonText) const;
    void _log(const QString &msg, bool errorLevel = false);
    void _openLogFile();

    IPageRepository &m_repo;
    CategoryTable   &m_categoryTable;
    QDir             m_workingDir;

    QNetworkAccessManager *m_nam       = nullptr;
    QNetworkReply         *m_reply     = nullptr;
    QList<TranslationJob>  m_queue;
    TranslationJob         m_currentJob;
    int                    m_translated = 0;
    int                    m_errors     = 0;
    QString                m_apiKey;

    // Log file — opened lazily on first _log() call, appended throughout.
    QFile *m_logFile = nullptr;
};

#endif // PAGETRANSLATOR_H
