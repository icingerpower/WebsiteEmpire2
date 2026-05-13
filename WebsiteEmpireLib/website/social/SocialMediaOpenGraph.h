#ifndef SOCIALMEDIAOPENGRAPH_H
#define SOCIALMEDIAOPENGRAPH_H

#include "website/social/AbstractSocialMedia.h"

/**
 * Open Graph protocol — covers Facebook, WhatsApp, Telegram, Slack,
 * Discord, iMessage, Signal, Mastodon, and any other consumer that reads
 * og: meta tags.
 *
 * Emits og:image (with width/height for scrapers that require explicit
 * dimensions), og:title, and og:description.
 * Image size: Landscape (1200×630).
 */
class SocialMediaOpenGraph : public AbstractSocialMedia
{
public:
    QString   getId()               const override;
    QString   getName()             const override;
    ImageSize requiredImageSize()   const override;
    QString   imageMetaTagHtml(const QString &imageUrl) const override;
    QString   titleMetaTagHtml(const QString &title)    const override;
    QString   descMetaTagHtml(const QString &desc)      const override;
};

#endif // SOCIALMEDIAOPENGRAPH_H
