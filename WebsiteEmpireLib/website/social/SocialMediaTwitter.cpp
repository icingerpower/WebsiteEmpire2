#include "SocialMediaTwitter.h"

#include <QCoreApplication>

QString SocialMediaTwitter::getId() const
{
    return QStringLiteral("twitter");
}

QString SocialMediaTwitter::getName() const
{
    return QCoreApplication::translate("SocialMediaTwitter", "Twitter / X (large card)");
}

AbstractSocialMedia::ImageSize SocialMediaTwitter::requiredImageSize() const
{
    return ImageSize::Landscape;
}

QString SocialMediaTwitter::imageMetaTagHtml(const QString &imageUrl) const
{
    QString html;
    html += QStringLiteral("<meta name=\"twitter:card\" content=\"summary_large_image\" />\n");
    html += QStringLiteral("<meta name=\"twitter:image\" content=\"");
    html += imageUrl;
    html += QStringLiteral("\" />\n");
    return html;
}

QString SocialMediaTwitter::titleMetaTagHtml(const QString &title) const
{
    if (title.isEmpty()) {
        return {};
    }
    QString html;
    html += QStringLiteral("<meta name=\"twitter:title\" content=\"");
    html += title.toHtmlEscaped();
    html += QStringLiteral("\" />\n");
    return html;
}

QString SocialMediaTwitter::descMetaTagHtml(const QString &desc) const
{
    if (desc.isEmpty()) {
        return {};
    }
    QString html;
    html += QStringLiteral("<meta name=\"twitter:description\" content=\"");
    html += desc.toHtmlEscaped();
    html += QStringLiteral("\" />\n");
    return html;
}

DECLARE_SOCIAL_MEDIA(SocialMediaTwitter)
