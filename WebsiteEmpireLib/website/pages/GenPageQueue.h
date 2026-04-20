#ifndef GENPAGEQUEUE_H
#define GENPAGEQUEUE_H

#include "website/pages/PageRecord.h"

#include <QHash>
#include <QList>
#include <QString>

class AbstractEngine;
class CategoryTable;
class IPageRepository;

/**
 * Job queue for AI content generation of source pages belonging to one
 * GenStrategyTable row.
 *
 * On construction, loads all pending pages via
 * IPageRepository::findPendingByTypeId() and optionally caps the total at
 * limit (−1 = unlimited).  The caller pops pages one at a time, sends the
 * built prompt to the Claude API, and calls processReply() with the response.
 *
 * Prompt building — two-step strategy
 * ------------------------------------
 * buildStep1Prompt() asks Claude to write content freely (no JSON constraints).
 * buildStep2Prompt() asks Claude to reformat its draft into the JSON schema.
 * The caller sends step1, appends Claude's reply, then sends step2 as the
 * next user turn so the model already has the full draft in context.
 * processReply() is called only on step2's output.
 *
 * Reply processing
 * ----------------
 * processReply() validates, saves via saveData(), and stamps generated_at.
 * Keys not present in the schema are silently dropped; missing keys default
 * to an empty string so the page always has a complete data set.
 */
class GenPageQueue
{
public:
    /**
     * pageTypeId          — stable id used with AbstractPageType::createForTypeId()
     * nonSvgImages        — passed through to the prompt as a generation hint
     * customInstructions  — strategy-level extra instructions appended to step-1 prompt
     * limit               — max pages to pop from this queue (−1 = all pending)
     */
    GenPageQueue(const QString   &pageTypeId,
                 bool             nonSvgImages,
                 IPageRepository &pageRepo,
                 CategoryTable   &categoryTable,
                 const QString   &customInstructions = {},
                 int              limit = -1);

    /**
     * Source-DB-backed constructor: caller supplies pre-built PageRecord items
     * with id == 0 (not yet persisted).  LauncherGeneration calls
     * IPageRepository::create() for each page just before saving its content.
     * No limit is applied — the caller truncates virtualPages before passing it.
     */
    GenPageQueue(const QString          &pageTypeId,
                 bool                    nonSvgImages,
                 const QList<PageRecord> &virtualPages,
                 CategoryTable          &categoryTable,
                 const QString          &customInstructions = {});

    bool             hasNext()   const;
    const PageRecord &peekNext() const;
    void             advance();        // pop the front; call after processReply()
    int              pendingCount() const;

    /**
     * Step 1 prompt: asks Claude to write content freely without JSON constraints.
     * Send this as the first user turn to get a high-quality draft.
     *
     * extraContext — optional per-page context for improvement passes (e.g.
     *   "Previous strategy did not get any impressions in Google Search. Try a
     *   different angle or keyword focus.").  Empty for normal generation.
     */
    QString buildStep1Prompt(const PageRecord &page,
                             AbstractEngine   &engine,
                             int               websiteIndex,
                             const QString    &extraContext = {}) const;

    /**
     * Step 2 prompt: asks Claude to reformat its previous free-form draft into
     * the JSON schema.  Send this as the third turn (after Claude's step-1 reply)
     * so the model already has the full content context.
     */
    QString buildStep2Prompt() const;

    /**
     * Parses responseText (Claude's text reply), saves the key→value data via
     * IPageRepository::saveData(), and stamps generated_at.
     * Returns true on success, false if the reply could not be parsed.
     */
    bool processReply(int             pageId,
                      const QString  &responseText,
                      IPageRepository &pageRepo);

private:
    // Returns the content schema for this page type: key → "" (all empty).
    // Built lazily and cached; requires CategoryTable for type instantiation.
    const QHash<QString, QString> &_schema() const;

    static QHash<QString, QString> _parseJson(const QString &text);

    QString          m_pageTypeId;
    bool             m_nonSvgImages;
    QString          m_customInstructions;
    CategoryTable   &m_categoryTable;
    QList<PageRecord> m_pending;
    int               m_cursor = 0;

    mutable QHash<QString, QString> m_schema;
    mutable bool                    m_schemaCached = false;
};

#endif // GENPAGEQUEUE_H
