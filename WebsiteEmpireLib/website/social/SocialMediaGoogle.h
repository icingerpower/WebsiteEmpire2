#ifndef SOCIALMEDIAGOOGLE_H
#define SOCIALMEDIAGOOGLE_H

#include "website/social/AbstractSocialMedia.h"

/**
 * Google Search / Google Discover — schema.org structured data.
 *
 * Emits an itemprop="image" meta tag and a minimal JSON-LD ImageObject
 * snippet that Google uses for rich results and Discover cards.  Google
 * requires images to be at least 1200 px wide for large rich-result previews.
 *
 * Image size: Wide (1200×675, 16:9) — the ratio Google prefers for articles.
 */
class SocialMediaGoogle : public AbstractSocialMedia
{
public:
    QString   getId()               const override;
    QString   getName()             const override;
    ImageSize requiredImageSize()   const override;
    QString   imageMetaTagHtml(const QString &imageUrl) const override;
    QString   titleMetaTagHtml(const QString &title)    const override;
    QString   descMetaTagHtml(const QString &desc)      const override;
};

#endif // SOCIALMEDIAGOOGLE_H
