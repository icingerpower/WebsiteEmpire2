#include "SocialMediaTwitterSummary.h"

#include <QCoreApplication>

QString SocialMediaTwitterSummary::getId() const
{
    return QStringLiteral("twitter_summary");
}

QString SocialMediaTwitterSummary::getName() const
{
    return QCoreApplication::translate("SocialMediaTwitterSummary", "Twitter / X (summary card)");
}

AbstractSocialMedia::ImageSize SocialMediaTwitterSummary::requiredImageSize() const
{
    return ImageSize::Square;
}

QString SocialMediaTwitterSummary::imageMetaTagHtml(const QString &imageUrl) const
{
    QString html;
    html += QStringLiteral("<meta name=\"twitter:card\" content=\"summary\" />\n");
    html += QStringLiteral("<meta name=\"twitter:image\" content=\"");
    html += imageUrl;
    html += QStringLiteral("\" />\n");
    return html;
}

QString SocialMediaTwitterSummary::titleMetaTagHtml(const QString &title) const
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

QString SocialMediaTwitterSummary::descMetaTagHtml(const QString &desc) const
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

DECLARE_SOCIAL_MEDIA(SocialMediaTwitterSummary)
