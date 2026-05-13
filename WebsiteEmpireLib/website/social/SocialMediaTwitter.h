#ifndef SOCIALMEDIATWITTER_H
#define SOCIALMEDIATWITTER_H

#include "website/social/AbstractSocialMedia.h"

/**
 * Twitter / X — large card (summary_large_image).
 *
 * Emits twitter:card=summary_large_image, twitter:image, twitter:title,
 * twitter:description.  The large card displays the image prominently above
 * the title and is the recommended format for article sharing.
 *
 * Image size: Landscape (1200×630).
 */
class SocialMediaTwitter : public AbstractSocialMedia
{
public:
    QString   getId()               const override;
    QString   getName()             const override;
    ImageSize requiredImageSize()   const override;
    QString   imageMetaTagHtml(const QString &imageUrl) const override;
    QString   titleMetaTagHtml(const QString &title)    const override;
    QString   descMetaTagHtml(const QString &desc)      const override;
};

#endif // SOCIALMEDIATWITTER_H
