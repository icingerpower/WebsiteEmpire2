#ifndef SOCIALMEDIAPINTEREST_H
#define SOCIALMEDIAPINTEREST_H

#include "website/social/AbstractSocialMedia.h"

/**
 * Pinterest — prefers portrait (2:3) images for pins.
 *
 * Emits og:image with the portrait variant URL and explicit dimensions.
 * Pinterest reads og: tags for link previews; the portrait crop makes pins
 * look better in Pinterest's grid layout than a landscape crop would.
 *
 * Image size: Portrait (1000×1500).
 */
class SocialMediaPinterest : public AbstractSocialMedia
{
public:
    QString   getId()               const override;
    QString   getName()             const override;
    ImageSize requiredImageSize()   const override;
    QString   imageMetaTagHtml(const QString &imageUrl) const override;
    QString   titleMetaTagHtml(const QString &title)    const override;
    QString   descMetaTagHtml(const QString &desc)      const override;
};

#endif // SOCIALMEDIAPINTEREST_H
