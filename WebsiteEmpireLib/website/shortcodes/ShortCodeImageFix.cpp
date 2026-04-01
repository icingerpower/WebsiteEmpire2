#include "ShortCodeImageFix.h"

QString ShortCodeImageFix::getTag() const
{
    return QStringLiteral("IMGFIX");
}

AbstractShortCode::Translatable ShortCodeImageFix::idTranslatable() const
{
    return Translatable::No;
}

AbstractShortCode::Translatable ShortCodeImageFix::fileNameTranslatable() const
{
    return Translatable::Optional;
}

DECLARE_SHORTCODE(ShortCodeImageFix)
