#include "ShortCodeImageTr.h"

QString ShortCodeImageTr::getTag() const
{
    return QStringLiteral("IMGTR");
}

AbstractShortCode::Translatable ShortCodeImageTr::idTranslatable() const
{
    return Translatable::Yes;
}

AbstractShortCode::Translatable ShortCodeImageTr::fileNameTranslatable() const
{
    return Translatable::Yes;
}

DECLARE_SHORTCODE(ShortCodeImageTr)
