#ifndef SHORTCODEIMAGEFIX_H
#define SHORTCODEIMAGEFIX_H

#include "AbstractShortCodeImage.h"

/**
 * ShortCode [IMGFIX][/IMGFIX]
 *
 * Embeds an image whose URL is fixed (not translated) or optionally translated
 * manually.  Use this shortcode for images that are the same across most locales
 * but may occasionally be swapped by a translator.
 *
 * Arguments:
 *   id       (mandatory, Translatable::No)       — image identifier
 *   fileName (mandatory, Translatable::Optional) — image file name; translation
 *                                                  is at the caller's discretion
 *   alt      (mandatory, Translatable::Yes)      — HTML alt text
 *   width    (optional,  Translatable::No)       — HTML width (positive integer)
 *   height   (optional,  Translatable::No)       — HTML height (positive integer)
 *
 * Example:
 *   [IMGFIX id="hero" fileName="hero.jpg" alt="Hero banner" width="1200" height="400"][/IMGFIX]
 */
class ShortCodeImageFix : public AbstractShortCodeImage
{
public:
    QString getTag() const override;

    QDialog *createEditDialog(QWidget *parent = nullptr) const override;
    QString getButtonName() const override;
    QString getButtonToolTip() const override;

protected:
    Translatable idTranslatable() const override;
    Translatable fileNameTranslatable() const override;
};

#endif // SHORTCODEIMAGEFIX_H
