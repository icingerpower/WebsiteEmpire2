#include "ShortCodeLinkFix.h"

QString ShortCodeLinkFix::getTag() const
{
    return QStringLiteral("LINKFIX");
}

AbstractShortCode::ArgumentDef ShortCodeLinkFix::urlArgumentDef() const
{
    return { ID_URL, /*mandatory=*/true, /*allowedValues=*/{}, /*translatable=*/Translatable::No };
}

DECLARE_SHORTCODE(ShortCodeLinkFix)
