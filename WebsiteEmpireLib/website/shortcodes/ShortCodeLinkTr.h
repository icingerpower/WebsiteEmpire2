#ifndef SHORTCODELINKTR_H
#define SHORTCODELINKTR_H

#include "AbstractShortCodeLink.h"

/**
 * ShortCode [LINKTR][/LINKTR]
 *
 * Generates an anchor tag whose URL may vary per language (translatable).
 * Use this shortcode when the destination page has different URLs for different
 * locales (e.g. a product page that exists in translated versions).
 *
 * Arguments:
 *   url (mandatory, Translatable::Optional) — the destination URL; may be
 *                                             translated per locale at the
 *                                             caller's discretion
 *   rel (optional,  Translatable::No)       — HTML rel value; defaults to "dofollow"
 *
 * Output:
 *   <a href="url" rel="rel">inner content</a>
 *
 * Example:
 *   [LINKTR url="https://example.fr/produit" rel="sponsored"]Voir le produit[/LINKTR]
 */
class ShortCodeLinkTr : public AbstractShortCodeLink
{
public:
    QString getTag() const override;

    QDialog *createEditDialog(QWidget *parent = nullptr) const override;
    QString getButtonName() const override;
    QString getButtonToolTip() const override;

protected:
    /** url is mandatory and optionally translated per locale. */
    ArgumentDef urlArgumentDef() const override;
};

#endif // SHORTCODELINKTR_H
