#ifndef ABSTRACTSHORTCODEIMAGE_H
#define ABSTRACTSHORTCODEIMAGE_H

#include "AbstractShortCode.h"

/**
 * Shared base for image shortcodes ([IMGFIX] and [IMGTR]).
 *
 * Both produce:
 *   <img src="TODO.webp" alt="alt" data-pin-description="..." [width="w"] [height="h"] />
 *
 * TODO: replace "TODO.webp" with the resolved image path once the image
 *       lookup class (retrieving the file name from the id argument) is available.
 *
 * Every image also carries a data-pin-description attribute (caption, falling
 * back to alt) and addCode() emits Pinterest's pinit.js widget once per page
 * (guarded by jsDoneIds) so every image on the page gets a hover "Save"
 * button — see addCode() doc below.
 *
 * Shared arguments:
 *   id       (mandatory) — image identifier used by the future lookup class
 *   fileName (mandatory) — image file name; used by the lookup class
 *   alt      (mandatory, Translatable::Yes)  — HTML alt attribute; always translated
 *   width    (optional,  Translatable::No)   — HTML width attribute; positive integer
 *   height   (optional,  Translatable::No)   — HTML height attribute; positive integer
 *   caption  (optional,  Translatable::Yes)  — short visible caption rendered in a
 *                                              <figcaption> below the image; when
 *                                              absent, a plain <img> is emitted
 *                                              instead of a <figure>
 *
 * Subclasses differ only in the translatability of id and fileName:
 *   - ShortCodeImageFix: id Translatable::No,       fileName Translatable::Optional
 *   - ShortCodeImageTr:  id Translatable::Yes,       fileName Translatable::Yes
 *
 * Subclasses must implement:
 *   - getTag()              — unique tag (e.g. "IMGFIX")
 *   - idTranslatable()      — translatability of the id argument
 *   - fileNameTranslatable()— translatability of the fileName argument
 */
class AbstractShortCodeImage : public AbstractShortCode
{
public:
    static constexpr const char *ID_ID       = "id";
    static constexpr const char *ID_FILENAME = "fileName";
    static constexpr const char *ID_ALT      = "alt";
    static constexpr const char *ID_WIDTH    = "width";
    static constexpr const char *ID_HEIGHT   = "height";
    static constexpr const char *ID_CAPTION  = "caption";

    /** Returns {id, fileName, alt, width, height, caption} with subclass-specific translatabilities. */
    QList<ArgumentDef> availableArguments() const override;

    /**
     * Returns true when:
     *   - argId == ID_ID, ID_FILENAME, or ID_ALT: value is non-empty after trimming
     *   - argId == ID_WIDTH or ID_HEIGHT: value matches ^\d+$ (positive integer digits)
     *   - any other argId: true (unreachable after validate())
     */
    bool isArgumentValueValid(const QString &argId, const QString &value) const override;

    /**
     * Parses origContent and appends:
     *   <img src="/fileName" alt="alt" data-pin-description="..." [width="w"] [height="h"] />
     * or, when the caption argument is present:
     *   <figure><img .../><figcaption>caption</figcaption></figure>
     *
     * The src is built from the fileName argument, which is the bare URL
     * filename (e.g. "hero.webp") stored in image_names.filename.
     * Drogon's ImageController resolves it to the correct blob at serve time.
     * width and height attributes are omitted when the corresponding arguments
     * are absent.  css and cssDoneIds are left unchanged.
     *
     * data-pin-description is set from caption when present, otherwise from
     * alt (always present — mandatory argument), HTML-escaped since it sits
     * inside a quoted attribute. This lets Pinterest's widget (see below)
     * pre-fill a sensible description when a visitor pins the image.
     *
     * Also appends (once per page, guarded by jsDoneIds key "pinterest_pinit")
     * inline JS that injects Pinterest's pinit.js widget script. That widget
     * scans the page for images and adds a hover "Save" button automatically
     * — no page URL/description needs to be threaded through addCode()'s
     * scope, Pinterest's script reads document.location itself. The script
     * tag itself cannot be appended directly to `js`: callers wrap the whole
     * `js` accumulator in one page-level <script>...</script> block (see
     * AbstractPageType::generate()), and a literal "</script>" substring
     * inside that text would prematurely close it — so the widget is loaded
     * via a dynamically created <script> DOM element instead.
     */
    void addCode(QStringView     origContent,
                 AbstractEngine &engine,
                 int             websiteIndex,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

    /**
     * Builds the opening tag from the ShortCodeImageDialog's fields, e.g.
     *   [IMGFIX id="hero" fileName="hero.jpg" alt="Hero" width="1200"]
     * width and height are omitted when the spinbox is 0.
     * Uses getTag() so both IMGFIX and IMGTR produce the correct tag name.
     * Subclasses (ShortCodeImageFix, ShortCodeImageTr) inherit this.
     */
    QString getTextBegin(const QDialog *dialog) const override;

    /**
     * Returns "[/TAG]" where TAG comes from getTag().
     * Subclasses (ShortCodeImageFix, ShortCodeImageTr) inherit this.
     */
    QString getTextEnd(const QDialog *dialog) const override;

    /**
     * Reduces a fileName argument to its bare basename: strips any directory
     * part and leading slashes ("/images/foo.jpg", "images/foo.jpg" and
     * "foo.jpg" all yield "foo.jpg"). Trims surrounding whitespace.
     *
     * THE single source of truth for image file naming, applied both when
     * rendering the <img src> here and when the raster/SVG pipeline keys a
     * generated blob in images.db (GenPageQueue::parseImgFixRefs()). The two
     * MUST agree: a blob stored under one spelling and requested under another
     * silently renders as a broken image.
     *
     * Regression this guards (2026-09-07): buildContentPrompt() told the AI
     * both `fileName="image.jpg"` (bare) and, for non-SVG strategies,
     * "reference images as relative paths (e.g. /images/foo.jpg)". The AI
     * alternated between the two between runs. A "/images/…"-spelled fileName
     * produced src="//images/foo.jpg" (double slash) and stored the blob under
     * the literal key "/images/foo.jpg", so every lookup by basename missed and
     * every image on the page rendered broken — while generation itself
     * reported complete success.
     */
    static QString normalizedFileName(const QString &fileName);

protected:
    /** Translatability of the id argument — No for IMGFIX, Yes for IMGTR. */
    virtual Translatable idTranslatable() const = 0;

    /** Translatability of the fileName argument — Optional for IMGFIX, Yes for IMGTR. */
    virtual Translatable fileNameTranslatable() const = 0;
};

#endif // ABSTRACTSHORTCODEIMAGE_H
