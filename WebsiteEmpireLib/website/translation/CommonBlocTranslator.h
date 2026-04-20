#ifndef COMMONBLOCTRANSLATOR_H
#define COMMONBLOCTRANSLATOR_H

#include "website/WebCodeAdder.h"

#include <QDir>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class AbstractCommonBloc;
class AbstractTheme;
class QFile;
class QNetworkAccessManager;
class QNetworkReply;

/**
 * Translates common bloc fields (header, footer, cookies, …) to every target
 * language for which translations are currently missing.
 *
 * Workflow
 * --------
 * 1. buildJobs() scans a list of blocs and produces one job per
 *    (bloc × targetLang) pair that has at least one untranslated field.
 *    Pairs where targetLang == sourceLang are skipped.
 * 2. startWithJobs() dispatches those jobs to the Claude API one at a time.
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
 * API key
 * -------
 * Read from the ANTHROPIC_API_KEY environment variable.
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
        QString sourceLang;  ///< language the source texts are written in
        QString targetLang;  ///< language to translate into
    };

    explicit CommonBlocTranslator(AbstractTheme &theme,
                                   const QDir    &workingDir,
                                   QObject       *parent = nullptr);
    ~CommonBlocTranslator() override;

    /**
     * Returns one job per (bloc × targetLang) pair that has at least one
     * field returned by missingTranslations().  Pairs where
     * targetLang == sourceLang are skipped.
     * The returned list is ready to pass to startWithJobs().
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
    void _onReplyFinished();

private:
    void    _processNextJob();
    QString _buildPrompt(const QList<TranslatableField> &fields,
                          const QString                  &sourceLang,
                          const QString                  &targetLang) const;
    QHash<QString, QString> _parseResponse(const QString &jsonText) const;
    void    _log(const QString &msg, bool errorLevel = false);
    void    _openLogFile();

    AbstractTheme            &m_theme;
    QDir                      m_workingDir;
    QString                   m_apiKey;

    QNetworkAccessManager    *m_nam         = nullptr;
    QNetworkReply            *m_reply       = nullptr;
    QList<TranslationJob>     m_queue;
    TranslationJob            m_currentJob;
    AbstractCommonBloc       *m_currentBloc = nullptr; ///< non-owning; valid during one API call
    int                       m_translated  = 0;
    int                       m_errors      = 0;

    QFile                    *m_logFile     = nullptr;
};

#endif // COMMONBLOCTRANSLATOR_H
