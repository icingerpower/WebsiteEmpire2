#ifndef PAGEFLAG_H
#define PAGEFLAG_H

#include <QFlags>
#include <QtGlobal>

/**
 * Bitmask flags stored in the pages.flags column.
 *
 * SocialMedia  — second-pass social image generation is enabled for this page.
 *                Set by --review when display count reaches the threshold, or
 *                manually via the PanePages UI.  Clearing the flag does NOT
 *                delete existing social image blobs in images.db.
 *
 * NeedsRewrite — the page is a candidate for AI content regeneration because
 *                its performance metrics are below expectations.  Set by --review.
 *
 * NeedsAds     — ad integration snippets should be injected on the next
 *                generation or serve pass.  Reserved for future use.
 *
 * IPageRepository::setFlag() sets or clears a single bit atomically.
 * When SocialMedia is set to true and the page is currently Complete,
 * setFlag() also resets generation_state to MainImageReady so that the
 * next --generation run picks up the page for the social-media second pass.
 */
enum class PageFlag : quint32 {
    SocialMedia  = 1 << 0,
    NeedsRewrite = 1 << 1,
    NeedsAds     = 1 << 2,
};
Q_DECLARE_FLAGS(PageFlags, PageFlag)
Q_DECLARE_OPERATORS_FOR_FLAGS(PageFlags)

#endif // PAGEFLAG_H
