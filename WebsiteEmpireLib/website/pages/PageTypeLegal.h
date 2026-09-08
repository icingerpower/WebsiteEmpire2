#ifndef PAGETYPELEGAL_H
#define PAGETYPELEGAL_H

#include "PageTypeArticleHealth.h"

/**
 * Legal page type — identical structure to the Health article vertical but
 * stored under a distinct TYPE_ID so legal pages can be filtered/generated
 * separately.
 *
 * Only getTypeId() and getDisplayName() are overridden; all bloc logic
 * is inherited from PageTypeArticleHealth.
 *
 * addInnerTopCode() is suppressed so the AI disclaimer does not appear
 * on legal pages — they are human-authored authoritative documents.
 */
class PageTypeLegal : public PageTypeArticleHealth
{
public:
    static constexpr const char *TYPE_ID      = "legal";
    static constexpr const char *DISPLAY_NAME = "Legal";

    using PageTypeArticleHealth::PageTypeArticleHealth;

    QString getTypeId()      const override;
    QString getDisplayName() const override;

    /** Legal pages (privacy policy, ToS, etc.) must never appear in search results. */
    bool shouldIndex() const override;

protected:
    void addInnerTopCode(AbstractEngine &engine,
                             int             websiteIndex,
                             QString        &html,
                             QString        &css,
                             QString        &js,
                             QSet<QString>  &cssDoneIds,
                             QSet<QString>  &jsDoneIds) const override;
};

#endif // PAGETYPELEGAL_H
