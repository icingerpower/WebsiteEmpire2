#include "PageStateFormatter.h"

#include "website/pages/PageFlag.h"
#include "website/pages/PageGenerationState.h"

#include <QStringList>

namespace PageStateFormatter {

QString formatGenerationState(int stateInt)
{
    switch (static_cast<PageGenerationState>(stateInt)) {
    case PageGenerationState::Pending:        return QStringLiteral("Pending");
    case PageGenerationState::ContentReady:   return QStringLiteral("Content");
    case PageGenerationState::MainImageReady: return QStringLiteral("Image");
    case PageGenerationState::Complete:        return QStringLiteral("Complete");
    case PageGenerationState::SocialComplete:  return QStringLiteral("CompleteWithSocial");
    }
    return QStringLiteral("?");
}

QString formatFlags(quint32 flags)
{
    QStringList parts;
    if (flags & static_cast<quint32>(PageFlag::SocialMedia))  { parts.append(QStringLiteral("S")); }
    if (flags & static_cast<quint32>(PageFlag::NeedsRewrite)) { parts.append(QStringLiteral("R")); }
    if (flags & static_cast<quint32>(PageFlag::NeedsAds))     { parts.append(QStringLiteral("A")); }
    return parts.join(QStringLiteral(" "));
}

} // namespace PageStateFormatter
