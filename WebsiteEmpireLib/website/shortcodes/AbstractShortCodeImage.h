#ifndef ABSTRACTSHORTCODEIMAGE_H
#define ABSTRACTSHORTCODEIMAGE_H

#include "AbstractShortCode.h"

/**
 * Shared base for image shortcodes ([IMGFIX] and [IMGTR]).
 *
 * Both produce:
 *   <img src="TODO.webp" alt="alt" [width="w"] [height="h"] />
 *
 * TODO: replace "TODO.webp" with the resolved image path once the image
 *       lookup class (retrieving the file name from the id argument) is available.
 *
 * Shared arguments:
 *   id       (mandatory) — image identifier used by the future lookup class
 *   fileName (mandatory) — image file name; used by the lookup class
 *   alt      (mandatory, Translatable::Yes)  — HTML alt attribute; always translated
 *   width    (optional,  Translatable::No)   — HTML width attribute; positive integer
 *   height   (optional,  Translatable::No)   — HTML height attribute; positive integer
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

    /** Returns {id, fileName, alt, width, height} with subclass-specific translatabilities. */
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
     *   <img src="TODO.webp" alt="alt" [width="w"] [height="h"] />
     *
     * width and height attributes are omitted when the corresponding arguments
     * are absent.  css, js, cssDoneIds and jsDoneIds are left unchanged.
     *
     * TODO: replace "TODO.webp" with the resolved image path retrieved from
     *       the image lookup class using the id argument once it is available.
     */
    void addCode(QStringView     origContent,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

protected:
    /** Translatability of the id argument — No for IMGFIX, Yes for IMGTR. */
    virtual Translatable idTranslatable() const = 0;

    /** Translatability of the fileName argument — Optional for IMGFIX, Yes for IMGTR. */
    virtual Translatable fileNameTranslatable() const = 0;
};

#endif // ABSTRACTSHORTCODEIMAGE_H
