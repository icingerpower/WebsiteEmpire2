#include "SocialMediaGoogle.h"

#include <QCoreApplication>

QString SocialMediaGoogle::getId() const
{
    return QStringLiteral("google");
}

QString SocialMediaGoogle::getName() const
{
    return QCoreApplication::translate("SocialMediaGoogle", "Google Search / Discover");
}

AbstractSocialMedia::ImageSize SocialMediaGoogle::requiredImageSize() const
{
    return ImageSize::Wide;
}

QString SocialMediaGoogle::imageMetaTagHtml(const QString &imageUrl) const
{
    const QSize &dims = imageSizeDimensions(ImageSize::Wide);
    QString html;
    html += QStringLiteral("<meta itemprop=\"image\" content=\"");
    html += imageUrl;
    html += QStringLiteral("\" />\n");
    // JSON-LD ImageObject for Google rich results
    html += QStringLiteral("<script type=\"application/ld+json\">"
                           "{\"@context\":\"https://schema.org\","
                           "\"@type\":\"ImageObject\","
                           "\"url\":\"");
    html += imageUrl;
    html += QStringLiteral("\",\"width\":");
    html += QString::number(dims.width());
    html += QStringLiteral(",\"height\":");
    html += QString::number(dims.height());
    html += QStringLiteral("}</script>\n");
    return html;
}

QString SocialMediaGoogle::titleMetaTagHtml(const QString & /*title*/) const
{
    return {};
}

QString SocialMediaGoogle::descMetaTagHtml(const QString & /*desc*/) const
{
    return {};
}

DECLARE_SOCIAL_MEDIA(SocialMediaGoogle)
