#include "AbstractSocialMedia.h"

// =============================================================================
// Registry
// =============================================================================

static QList<AbstractSocialMedia *> &registry()
{
    static QList<AbstractSocialMedia *> s_all;
    return s_all;
}

void AbstractSocialMedia::registerPlatform(AbstractSocialMedia *platform)
{
    registry().append(platform);
}

const QList<AbstractSocialMedia *> &AbstractSocialMedia::all()
{
    return registry();
}

// =============================================================================
// Helpers
// =============================================================================

QSize AbstractSocialMedia::imageSizeDimensions(ImageSize size)
{
    switch (size) {
    case ImageSize::Landscape: return {1200, 630};
    case ImageSize::Wide:      return {1200, 675};
    case ImageSize::Square:    return {1200, 1200};
    case ImageSize::Portrait:  return {1000, 1500};
    }
    return {1200, 630};
}

QString AbstractSocialMedia::svgSuffix(ImageSize size)
{
    switch (size) {
    case ImageSize::Landscape: return QStringLiteral("-og.svg");
    case ImageSize::Wide:      return QStringLiteral("-wide.svg");
    case ImageSize::Square:    return QStringLiteral("-square.svg");
    case ImageSize::Portrait:  return QStringLiteral("-portrait.svg");
    }
    return QStringLiteral("-og.svg");
}

QString AbstractSocialMedia::webpSuffix(ImageSize size)
{
    switch (size) {
    case ImageSize::Landscape: return QStringLiteral("-og.webp");
    case ImageSize::Wide:      return QStringLiteral("-wide.webp");
    case ImageSize::Square:    return QStringLiteral("-square.webp");
    case ImageSize::Portrait:  return QStringLiteral("-portrait.webp");
    }
    return QStringLiteral("-og.webp");
}
