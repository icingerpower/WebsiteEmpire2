#ifndef REVIEWACTIONSOCIALMEDIA_H
#define REVIEWACTIONSOCIALMEDIA_H

#include "website/review/AbstractReviewAction.h"

/**
 * Review action that enables the social-media second pass (PageFlag::SocialMedia)
 * once a page's display count reaches the threshold declared by its secondary blocs.
 *
 * For each source page the action:
 *  1. Loads the page type and checks whether any bloc is an
 *     AbstractSecondaryPageBloc.
 *  2. Takes the minimum getDisplayThreshold() across all such blocs as the
 *     required display count.
 *  3. If displayCount >= threshold AND the page does not already have the
 *     SocialMedia flag, calls repo.setFlag(id, SocialMedia, true) and returns
 *     true.
 *
 * Pages that already have the flag are skipped (no redundant SQL writes).
 * Pages whose page type has no secondary blocs are also skipped immediately.
 */
class ReviewActionSocialMedia : public AbstractReviewAction
{
public:
    static constexpr const char *ID           = "social_media";
    static constexpr const char *DISPLAY_NAME = "Social Media";

    QString getId()          const override;
    QString getDisplayName() const override;

    bool run(const PageRecord &page,
             int               displayCount,
             IPageRepository  &repo) override;
};

#endif // REVIEWACTIONSOCIALMEDIA_H
