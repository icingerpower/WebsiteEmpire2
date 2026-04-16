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
 * Prompt building
 * ---------------
 * buildPrompt() discovers the content schema by constructing a fresh
 * AbstractPageType instance (via the registry) and calling save() on it.
 * The resulting empty key→value map forms the JSON skeleton sent to Claude.
 * Claude must return a JSON object with the same keys filled in.
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
     * pageTypeId    — stable id used with AbstractPageType::createForTypeId()
     * nonSvgImages  — passed through to the prompt as a generation hint
     * limit         — max pages to pop from this queue (−1 = all pending)
     */
    GenPageQueue(const QString   &pageTypeId,
                 bool             nonSvgImages,
                 IPageRepository &pageRepo,
                 CategoryTable   &categoryTable,
                 int              limit = -1);

    bool             hasNext()   const;
    const PageRecord &peekNext() const;
    void             advance();        // pop the front; call after processReply()
    int              pendingCount() const;

    /**
     * Builds the user-message string to send to the Claude API for page.
     * The message asks Claude to fill in all content fields and return JSON.
     */
    QString buildPrompt(const PageRecord &page,
                        AbstractEngine   &engine,
                        int               websiteIndex) const;

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
    CategoryTable   &m_categoryTable;
    QList<PageRecord> m_pending;
    int               m_cursor = 0;

    mutable QHash<QString, QString> m_schema;
    mutable bool                    m_schemaCached = false;
};

#endif // GENPAGEQUEUE_H
