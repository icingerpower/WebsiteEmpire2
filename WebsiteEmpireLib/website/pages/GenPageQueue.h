#ifndef GENPAGEQUEUE_H
#define GENPAGEQUEUE_H

#include "website/pages/PageRecord.h"

#include <QDir>
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
     * svgInstructions     — non-empty enables the SVG generation pass; used verbatim
     *                       in buildSvgPrompt() as the design requirements section
     * imageInstructions   — non-empty enables the raster image generation pass
     *                       (see wantsRasterImage()); used verbatim in
     *                       buildRasterImagePrompt() as the style/requirements section
     *                       and folded into buildContentPrompt() so the article-writing
     *                       call knows to produce raster [IMGFIX] references
     * imageCountMin/Max   — 0/0 = unenforced; otherwise buildContentPrompt() states the
     *                       exact range to the AI (see GenStrategyTable for the UI)
     * limit               — max pages to pop from this queue (−1 = all pending)
     */
    GenPageQueue(const QString   &pageTypeId,
                 bool             nonSvgImages,
                 IPageRepository &pageRepo,
                 CategoryTable   &categoryTable,
                 const QString   &customInstructions = {},
                 const QString   &svgInstructions    = {},
                 int              limit = -1,
                 const QDir      &workingDir = QDir{},
                 const QString   &imageInstructions = {},
                 int              imageCountMin = 0,
                 int              imageCountMax = 0);

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
                 const QString          &customInstructions = {},
                 const QString          &svgInstructions    = {},
                 const QDir             &workingDir = QDir{},
                 const QString          &imageInstructions = {},
                 int                     imageCountMin = 0,
                 int                     imageCountMax = 0);

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
     * Returns true if articleText contains at least one [IMGFIX] whose fileName
     * ends with ".svg" (case-insensitive).
     */
    static bool hasSvgImgFix(const QString &articleText);

    /**
     * Counts [IMGFIX] refs in articleText whose fileName does NOT end in ".svg"
     * — i.e. the raster (photo) images that imageCountMin()/imageCountMax()
     * apply to. SVG refs are excluded because their count is never
     * strategy-configurable.
     *
     * Counts distinct occurrences, not distinct ids: the count must match what
     * LauncherGeneration's own raster loop will iterate over.
     */
    static int countRasterImgFixRefs(const QString &articleText);

    /**
     * Returns true when the strategy has dedicated SVG instructions, i.e. the
     * SVG generation pass should run after the article content pass.
     */
    bool wantsSvgImage() const;

    /**
     * Returns true when the strategy has dedicated raster image instructions,
     * i.e. the raster image generation pass should run for [IMGFIX] refs whose
     * fileName does not end in ".svg" after the article content pass.
     */
    bool wantsRasterImage() const;

    /**
     * Returns the strategy's raw raster image style/requirements text (empty
     * when wantsRasterImage() is false).  Exposed so callers building a
     * separate review prompt (grading a generated image against the same
     * style requirements used in buildRasterImagePrompt()) don't have to
     * duplicate the strategy's imageInstructions text.
     */
    QString rasterImageInstructions() const;

    /**
     * Returns the configured minimum/maximum number of raster images for this
     * strategy (0/0 = unenforced).  Exposed so LauncherGeneration can: (1) trim
     * an over-long AI response to at most imageCountMax() raster refs before
     * attempting generation, bounding the CLI-call cost of a single page; and
     * (2) require at least imageCountMin() raster refs to have been found
     * before a page is allowed to reach PageGenerationState::Complete — an
     * under-count leaves the page in ContentReady, where the next generation
     * run's content regeneration gets another chance at a better count, the
     * same fallback already used when SVG generation comes up short.
     */
    int imageCountMin() const;
    int imageCountMax() const;

    /**
     * Builds a repair prompt that asks Claude to return ONLY the missing
     * [IMGFIX ... fileName="*.svg" ...][/IMGFIX] shortcode that should be
     * inserted into the article.  Used when wantsSvgImage() is true but
     * hasSvgImgFix() returns false after the content call.
     */
    QString buildSvgRepairPrompt(const PageRecord &page,
                                  const QString   &articleText,
                                  const QString   &lang) const;

    /**
     * Builds a repair prompt asking the AI to extend the article with `missing`
     * further outfit sections, each carrying its own raster [IMGFIX], and to
     * return ONLY those new sections.
     *
     * Used when the content call produced fewer raster refs than
     * imageCountMin(). Without this the shortfall was unrecoverable: an
     * under-counted page stayed ContentReady, but the retry path skips content
     * generation entirely, so it could never gain the missing images and was
     * eventually marked Complete below the minimum.
     *
     * Asks for whole sections rather than bare [IMGFIX] tags (unlike
     * buildSvgRepairPrompt) because a raster image must sit inside the outfit
     * recommendation it illustrates — extractRelevantSection() slices the
     * article on [TITLE] boundaries to scope each image's prompt and review, so
     * an image with no section of its own would be reviewed against the wrong
     * text.
     */
    QString buildRasterCountRepairPrompt(const PageRecord &page,
                                          const QString   &articleText,
                                          const QString   &lang,
                                          int              missing) const;

    /**
     * Inserts imgFixCode into articleText just before the last [TITLE level="2"]
     * heading.  Falls back to appending at the end if no such heading is found.
     */
    static QString insertImgFix(const QString &articleText,
                                 const QString &imgFixCode);

    /**
     * Returns the portion of articleText most relevant to the [IMGFIX
     * id="imgFixId" ...] occurrence — everything from the nearest preceding
     * [TITLE level="N"] heading up to the next [TITLE] heading (or the end of
     * the article). Used to scope both image generation and image review to
     * the one section an image actually illustrates, instead of the whole
     * article — so a multi-section article (e.g. several "type" sections,
     * each with its own outfit recommendation) can never have one section's
     * image tailored to, or validated against, another section's unrelated
     * recommendation.
     *
     * Deterministic string search — no AI call. Falls back to the full
     * articleText (never returns something narrower than the truth) when:
     * imgFixId is empty, no [IMGFIX id="imgFixId" ...] occurrence is found in
     * articleText, or no [TITLE] heading precedes that occurrence (e.g. it
     * sits in the article's opening paragraph, before any heading).
     */
    static QString extractRelevantSection(const QString &articleText,
                                           const QString &imgFixId);

    /**
     * Fixes a known AI mistake where the first [TITLE level="1"]...[/TITLE]
     * begins with the raw permalink slug instead of a human-readable topic
     * name — e.g. "[TITLE level=\"1\"]/osteoarthritis-dos-and-dont Dos and
     * Don'ts: ...[/TITLE]" instead of "[TITLE level=\"1\"]Osteoarthritis Dos
     * and Don'ts: ...[/TITLE]".
     *
     * When the title text starts with permalink (with or without its leading
     * '/', case-insensitive), the matched prefix and any following separator
     * punctuation are replaced with a humanized topic name: permalink with
     * the leading '/' and, when endPermalink is non-empty and the slug ends
     * with "-<endPermalink>", that trailing suffix removed too, then title-
     * cased with hyphens turned into spaces. Stripping the suffix isolates
     * the actual subject from the strategy-added wording (e.g. "dos-and-
     * dont", "genes-biomarkers"), which is unrelated to the topic itself.
     *
     * Returns articleText unchanged when the title does not start with the
     * permalink slug, or when no [TITLE level="1"] shortcode is found.
     */
    static QString fixSlugTitle(const QString &articleText,
                                 const QString &permalink,
                                 const QString &endPermalink);

    /**
     * Result of parseSvgDelimitedResponse().
     * svgCode is empty when no valid SVG was found in the response.
     * insertAfter is empty when no insertion hint was provided; callers should
     * fall back to the insertImgFix() heuristic in that case.
     */
    struct SvgGenerationResult {
        QString svgCode;
        QString insertAfter;
    };

    /**
     * Parses a Claude response that uses the delimiter format:
     *   ===SVG===
     *   <svg ...>...</svg>
     *   ===INSERT_AFTER===        (optional)
     *   <heading or sentence>
     *
     * Falls back to extracting the first <svg>…</svg> block when no ===SVG===
     * marker is present, for backward compatibility with direct SVG responses.
     */
    static SvgGenerationResult parseSvgDelimitedResponse(const QString &response);

    /**
     * Builds a Claude prompt that produces a standalone SVG for the given image
     * reference.  articleText is included for content context so Claude can
     * tailor the diagram to the article.  Uses m_svgInstructions as the design
     * requirements section.  Output is expected in the ===SVG=== delimiter format.
     */
    QString buildSvgPrompt(const ImgFixRef &ref,
                            const QString   &articleText,
                            const QString   &lang) const;

    /**
     * Builds a prompt requesting a real (raster, photorealistic-capable) image
     * for the given image reference, tagged with RASTER_IMAGE_PROMPT_MARKER
     * (see common/aicli/AbstractCli.h) so a CLI's preparePrompt() override can
     * detect it and switch to its own image-generation tool.  articleText is
     * included for context so the image reflects the article; m_imageInstructions
     * is used verbatim as the shared style/requirements section (equivalent to
     * m_svgInstructions in buildSvgPrompt()).  outputPath is the absolute path
     * the CLI must save the resulting image file to.
     *
     * Pure function of its inputs — safe to call again identically on a retry
     * or on the next generation run after a crash or CLI usage-limit error.
     */
    QString buildRasterImagePrompt(const ImgFixRef &ref,
                                    const QString   &articleText,
                                    const QString   &lang,
                                    const QString   &outputPath) const;

private:
    // Returns the content schema for this page type: key → "" (all empty).
    // Built lazily and cached; requires CategoryTable for type instantiation.
    const QHash<QString, QString> &_schema() const;

    // Returns per-key AI hints aggregated from all blocs via collectAiKeyClues().
    // Built lazily and cached alongside _schema().
    const QHash<QString, QString> &_aiKeyClues() const;

    static QHash<QString, QString> _parseJson(const QString &text);

    QString          m_pageTypeId;
    bool             m_nonSvgImages;
    QString          m_customInstructions;
    QString          m_svgInstructions;
    QString          m_imageInstructions;
    int              m_imageCountMin = 0;
    int              m_imageCountMax = 0;
    CategoryTable   &m_categoryTable;
    QDir             m_workingDir;
    QList<PageRecord> m_pending;
    int               m_cursor = 0;

    mutable QHash<QString, QString> m_schema;
    mutable bool                    m_schemaCached = false;
    mutable QHash<QString, QString> m_aiKeyClues;
    mutable bool                    m_aiKeyCluesCached = false;
};

#endif // GENPAGEQUEUE_H
