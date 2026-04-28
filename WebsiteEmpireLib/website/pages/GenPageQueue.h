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
 * Prompt building — two-call split strategy
 * -------------------------------------------
 * buildContentPrompt() asks Claude to write the article body as free-form text
 * with shortcodes.  This call succeeds for any article length because the
 * output has no JSON encoding overhead.
 *
 * buildMetadataPrompt() asks Claude to generate a small JSON object with all
 * schema fields EXCEPT 1_text.  The output is always small (~1 000 chars).
 *
 * processContentAndMetadata() combines the article text and metadata JSON into
 * the page record.
 *
 * buildCombinedPrompt() / buildStep1Prompt() / buildStep2Prompt() are retained
 * but are no longer called by the launchers.
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
     * Image reference parsed from an [IMGFIX ...] shortcode in article text.
     * Used by parseImgFixRefs() and buildSvgPrompt().
     */
    struct ImgFixRef {
        QString id;
        QString fileName;
        QString alt;
    };

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
     * Combined single-step prompt: asks Claude to write AND format the content
     * directly as JSON in one pass, eliminating the step-2 reproduction overhead.
     *
     * Use this instead of buildStep1Prompt() + buildStep2Prompt() whenever the
     * expected article length may be large, to avoid hitting Claude's output
     * token limit when reproducing the full draft as JSON values.
     *
     * extraContext — optional per-page context for improvement passes.
     */
    QString buildCombinedPrompt(const PageRecord &page,
                                 AbstractEngine   &engine,
                                 int               websiteIndex,
                                 const QString    &extraContext = {}) const;

    /**
     * Content-only prompt: asks Claude to write the article body as free-form text
     * with shortcodes (no JSON constraints).  For long articles this avoids the
     * output-token-limit issue that occurs when encoding the full text as a JSON
     * string value.  Feed the result to buildMetadataPrompt() and
     * processContentAndMetadata().
     *
     * extraContext — optional per-page context for improvement passes.
     */
    QString buildContentPrompt(const PageRecord &page,
                               AbstractEngine   &engine,
                               int               websiteIndex,
                               const QString    &extraContext = {}) const;

    /**
     * Metadata prompt: given the article text produced by buildContentPrompt(),
     * asks Claude to generate a small JSON object containing ALL schema fields
     * EXCEPT 1_text.  Output is always small (~1 000 chars), within token budget.
     *
     * articleText — the raw text returned by Claude for buildContentPrompt().
     */
    QString buildMetadataPrompt(const PageRecord &page,
                                 const QString   &articleText) const;

    /**
     * Saves articleText as the "1_text" field and all other fields from the
     * parsed metadataJson.  If metadataJson is empty or unparseable, only 1_text
     * is saved.  Stamps generated_at on success.
     * Returns true if at least 1_text was saved successfully.
     */
    bool processContentAndMetadata(int              pageId,
                                    const QString   &articleText,
                                    const QString   &metadataJson,
                                    IPageRepository &pageRepo);

    /**
     * Parses responseText (Claude's text reply), saves the key→value data via
     * IPageRepository::saveData(), and stamps generated_at.
     * Returns true on success, false if the reply could not be parsed.
     */
    bool processReply(int             pageId,
                      const QString  &responseText,
                      IPageRepository &pageRepo);

    /**
     * Extracts every [IMGFIX ...] occurrence from articleText and returns one
     * ImgFixRef per occurrence whose id and fileName are both non-empty.
     */
    static QList<ImgFixRef> parseImgFixRefs(const QString &articleText);

    /**
     * Builds a Claude prompt that produces a standalone SVG for the given image
     * reference.  permalink and lang give the article context.
     */
    static QString buildSvgPrompt(const ImgFixRef &ref,
                                   const QString   &permalink,
                                   const QString   &lang);

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
