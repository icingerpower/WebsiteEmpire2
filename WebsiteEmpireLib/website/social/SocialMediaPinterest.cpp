#include "SocialMediaPinterest.h"

#include <QCoreApplication>

QString SocialMediaPinterest::getId() const
{
    return QStringLiteral("pinterest");
}

QString SocialMediaPinterest::getName() const
{
    return QCoreApplication::translate("SocialMediaPinterest", "Pinterest");
}

AbstractSocialMedia::ImageSize SocialMediaPinterest::requiredImageSize() const
{
    return ImageSize::Portrait;
}

QString SocialMediaPinterest::imageMetaTagHtml(const QString &imageUrl) const
{
    const QSize &dims = imageSizeDimensions(ImageSize::Portrait);
    QString html;
    html += QStringLiteral("<meta property=\"og:image\" content=\"");
    html += imageUrl;
    html += QStringLiteral("\" />\n");
    html += QStringLiteral("<meta property=\"og:image:width\" content=\"");
    html += QString::number(dims.width());
    html += QStringLiteral("\" />\n");
    html += QStringLiteral("<meta property=\"og:image:height\" content=\"");
    html += QString::number(dims.height());
    html += QStringLiteral("\" />\n");
    return html;
}

QString SocialMediaPinterest::titleMetaTagHtml(const QString &title) const
{
    if (title.isEmpty()) {
        return {};
    }
    QString html;
    html += QStringLiteral("<meta property=\"og:title\" content=\"");
    html += title.toHtmlEscaped();
    html += QStringLiteral("\" />\n");
    return html;
}

QString SocialMediaPinterest::descMetaTagHtml(const QString &desc) const
{
    if (desc.isEmpty()) {
        return {};
    }
    QString html;
    html += QStringLiteral("<meta property=\"og:description\" content=\"");
    html += desc.toHtmlEscaped();
    html += QStringLiteral("\" />\n");
    return html;
}

DECLARE_SOCIAL_MEDIA(SocialMediaPinterest)
