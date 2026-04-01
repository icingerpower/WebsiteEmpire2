#ifndef SHORTCODEIMAGETR_H
#define SHORTCODEIMAGETR_H

#include "AbstractShortCodeImage.h"

/**
 * ShortCode [IMGTR][/IMGTR]
 *
 * Embeds an image whose id and file name must be translated for each locale.
 * Use this shortcode when the image itself (and therefore its identifier) varies
 * per language — for example, locale-specific product photos or infographics.
 *
 * Arguments:
 *   id       (mandatory, Translatable::Yes) — image identifier; must be translated
 *   fileName (mandatory, Translatable::Yes) — image file name; must be translated
 *   alt      (mandatory, Translatable::Yes) — HTML alt text
 *   width    (optional,  Translatable::No)  — HTML width (positive integer)
 *   height   (optional,  Translatable::No)  — HTML height (positive integer)
 *
 * Example:
 *   [IMGTR id="hero-fr" fileName="hero_fr.jpg" alt="Bannière principale" width="1200" height="400"][/IMGTR]
 */
class ShortCodeImageTr : public AbstractShortCodeImage
{
public:
    QString getTag() const override;

protected:
    Translatable idTranslatable() const override;
    Translatable fileNameTranslatable() const override;
};

#endif // SHORTCODEIMAGETR_H
