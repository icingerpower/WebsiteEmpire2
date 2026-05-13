#ifndef SOCIALMEDIATWITTERSUMMARY_H
#define SOCIALMEDIATWITTERSUMMARY_H

#include "website/social/AbstractSocialMedia.h"

/**
 * Twitter / X — summary card (1:1 square).
 *
 * Emits twitter:card=summary and twitter:image (square).  Useful when the
 * article SVG is designed to read well in a square crop.  This class is
 * complementary to SocialMediaTwitter (large card): both can coexist in the
 * registry; the large card takes precedence when both are present because
 * Twitter uses the last twitter:card value it sees.
 *
 * If only square cards are desired, remove SocialMediaTwitter from the build.
 *
 * Image size: Square (1200×1200).
 */
class SocialMediaTwitterSummary : public AbstractSocialMedia
{
public:
    QString   getId()               const override;
    QString   getName()             const override;
    ImageSize requiredImageSize()   const override;
    QString   imageMetaTagHtml(const QString &imageUrl) const override;
    QString   titleMetaTagHtml(const QString &title)    const override;
    QString   descMetaTagHtml(const QString &desc)      const override;
};

#endif // SOCIALMEDIATWITTERSUMMARY_H
