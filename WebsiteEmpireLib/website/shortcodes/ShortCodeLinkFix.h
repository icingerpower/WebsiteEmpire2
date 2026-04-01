#ifndef SHORTCODELINKFIX_H
#define SHORTCODELINKFIX_H

#include "AbstractShortCodeLink.h"

/**
 * ShortCode [LINKFIX][/LINKFIX]
 *
 * Generates an anchor tag with a fixed (non-translated) URL.
 * Use this shortcode for links that are identical in every language
 * (e.g. links to external studies or technical documents).
 *
 * Arguments:
 *   url (mandatory, Translatable::No)  — the destination URL
 *   rel (optional,  Translatable::No)  — HTML rel value; defaults to "dofollow"
 *
 * Output:
 *   <a href="url" rel="rel">inner content</a>
 *
 * Example:
 *   [LINKFIX url="https://example.com/study" rel="nofollow"]Read the study[/LINKFIX]
 */
class ShortCodeLinkFix : public AbstractShortCodeLink
{
public:
    QString getTag() const override;

protected:
    /** url is mandatory and never translated. */
    ArgumentDef urlArgumentDef() const override;
};

#endif // SHORTCODELINKFIX_H
