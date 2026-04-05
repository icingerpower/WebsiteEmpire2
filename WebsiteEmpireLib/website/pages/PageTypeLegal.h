#ifndef PAGETYPELEGAL_H
#define PAGETYPELEGAL_H

#include "PageTypeArticle.h"

/**
 * Legal page type — identical structure to Article but stored under a
 * distinct TYPE_ID so legal pages can be filtered/generated separately.
 *
 * Only getTypeId() and getDisplayName() are overridden; all bloc logic
 * is inherited from PageTypeArticle.
 */
class PageTypeLegal : public PageTypeArticle
{
public:
    static constexpr const char *TYPE_ID      = "legal";
    static constexpr const char *DISPLAY_NAME = "Legal";

    using PageTypeArticle::PageTypeArticle;

    QString getTypeId()      const override;
    QString getDisplayName() const override;
};

#endif // PAGETYPELEGAL_H
