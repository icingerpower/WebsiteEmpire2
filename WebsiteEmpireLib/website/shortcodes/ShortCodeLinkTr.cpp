#include "ShortCodeLinkTr.h"

QString ShortCodeLinkTr::getTag() const
{
    return QStringLiteral("LINKTR");
}

AbstractShortCode::ArgumentDef ShortCodeLinkTr::urlArgumentDef() const
{
    return { ID_URL, /*mandatory=*/true, /*allowedValues=*/{}, /*translatable=*/Translatable::Optional };
}

DECLARE_SHORTCODE(ShortCodeLinkTr)
