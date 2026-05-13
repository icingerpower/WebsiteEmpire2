#ifndef SOCIALMEDIALINKEDIN_H
#define SOCIALMEDIALINKEDIN_H

#include "website/social/AbstractSocialMedia.h"

/**
 * LinkedIn — uses og: tags but its scraper requires explicit og:image:width
 * and og:image:height to display the image reliably.  This class also emits
 * the og:image tag so that LinkedIn gets a fully self-contained set of tags
 * independently of SocialMediaOpenGraph.
 *
 * Image size: Landscape (1200×630).
 */
class SocialMediaLinkedIn : public AbstractSocialMedia
{
public:
    QString   getId()               const override;
    QString   getName()             const override;
    ImageSize requiredImageSize()   const override;
    QString   imageMetaTagHtml(const QString &imageUrl) const override;
    QString   titleMetaTagHtml(const QString &title)    const override;
    QString   descMetaTagHtml(const QString &desc)      const override;
};

#endif // SOCIALMEDIALINKEDIN_H
