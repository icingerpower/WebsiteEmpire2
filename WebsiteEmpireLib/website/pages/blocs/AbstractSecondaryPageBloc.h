#ifndef ABSTRACTSECONDARYPAGEBLOC_H
#define ABSTRACTSECONDARYPAGEBLOC_H

#include "website/pages/blocs/AbstractPageBloc.h"
#include "website/social/AbstractSocialMedia.h"

#include <QList>
#include <QString>

class PageRecord;

/**
 * Base class for blocs that require a second AI generation pass.
 *
 * The second pass runs after the main article pipeline is complete
 * (PageGenerationState::MainImageReady) and produces additional content that
 * depends on the finished article — in particular, social media image variants.
 *
 * Subclasses must implement:
 *   - buildSecondPassPrompts()  — one prompt string per social image variant
 *   - validateSecondPassResult() — validate / clean a single Claude response
 *   - svgBaseFileName()         — the article slug used to derive filenames
 *
 * All other AbstractPageBloc virtuals (load, save, addCode, createEditWidget,
 * getAiKeyClues) must also be implemented as usual; the second-pass logic is
 * orthogonal to the first-pass metadata.
 *
 * Workflow
 * --------
 * 1. LauncherGeneration detects isSecondTimeGeneration() == true on this bloc.
 * 2. It calls buildSecondPassPrompts() to get one prompt per ImageSize variant.
 * 3. Each prompt is sent to Claude; the raw response is passed to
 *    validateSecondPassResult() which returns a clean SVG string or throws.
 * 4. The validated SVG is written to images.db via ImageWriter::writeSvg().
 * 5. QSvgRenderer rasterises the SVG to WebP; ImageWriter::writeQImage() saves it.
 * 6. PageGenerationState advances to Complete.
 *
 * Translation
 * -----------
 * PageTranslator translates each SVG variant and immediately rasterises the
 * translated SVG to WebP, updating page_translation_image_states accordingly.
 */
class AbstractSecondaryPageBloc : public AbstractPageBloc
{
public:
    bool isSecondTimeGeneration() const final { return true; }

    /**
     * Returns one AI prompt per required ImageSize variant, in the order
     * returned by requiredImageSizes().
     *
     * sourceSvgContent is the raw SVG text of the primary [IMGFIX] image
     * already generated for the article.  Prompts should reference it to keep
     * the visual language consistent across variants.
     *
     * page carries permalink, lang, and other article metadata for context.
     */
    virtual QList<QString> buildSecondPassPrompts(const QString    &sourceSvgContent,
                                                   const PageRecord &page) const = 0;

    /**
     * Validates the raw Claude response for the variant at variantIndex
     * (matching the order of requiredImageSizes()).
     *
     * Returns the cleaned SVG string on success.
     * Throws ExceptionWithTitleText on validation failure so the launcher can
     * log the error and skip the variant without crashing.
     */
    virtual QString validateSecondPassResult(const QString &rawResult,
                                              int            variantIndex) const = 0;

    /**
     * Returns the image sizes this bloc needs, in the order that prompts and
     * results are paired.  The default returns all four sizes (one per platform
     * group).  Override to restrict to a subset if the page type does not need
     * all variants.
     */
    virtual QList<AbstractSocialMedia::ImageSize> requiredImageSizes() const;

    /**
     * Returns the filename stem (without extension or size suffix) shared by
     * all SVG and WebP variants.  Typically derived from the article slug.
     *
     * Example: if the article slug is "knee-surgery", this returns
     * "knee-surgery" and the variants will be named:
     *   knee-surgery-og.svg / knee-surgery-og.webp
     *   knee-surgery-wide.svg / knee-surgery-wide.webp
     *   knee-surgery-square.svg / knee-surgery-square.webp
     *   knee-surgery-portrait.svg / knee-surgery-portrait.webp
     */
    virtual QString svgBaseFileName() const = 0;

    /**
     * Returns the page-data key (without bloc-index prefix) that stores the
     * WebP filename for the given variant index.
     *
     * LauncherGeneration calls this after each WebP is written to construct the
     * prefixed key (e.g. "2_img_og") it inserts into the page-data map, then
     * persists the map via IPageRepository::saveData().
     */
    virtual QString variantDataKey(int variantIndex) const = 0;
};

#endif // ABSTRACTSECONDARYPAGEBLOC_H
