#include "SocialMediaOpenGraph.h"

#include <QCoreApplication>

QString SocialMediaOpenGraph::getId() const
{
    return QStringLiteral("opengraph");
}

QString SocialMediaOpenGraph::getName() const
{
    return QCoreApplication::translate("SocialMediaOpenGraph", "Open Graph (Facebook / WhatsApp / Slack…)");
}

AbstractSocialMedia::ImageSize SocialMediaOpenGraph::requiredImageSize() const
{
    return ImageSize::Landscape;
}

QString SocialMediaOpenGraph::imageMetaTagHtml(const QString &imageUrl) const
{
    const QSize &dims = imageSizeDimensions(ImageSize::Landscape);
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

QString SocialMediaOpenGraph::titleMetaTagHtml(const QString &title) const
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

QString SocialMediaOpenGraph::descMetaTagHtml(const QString &desc) const
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

DECLARE_SOCIAL_MEDIA(SocialMediaOpenGraph)
