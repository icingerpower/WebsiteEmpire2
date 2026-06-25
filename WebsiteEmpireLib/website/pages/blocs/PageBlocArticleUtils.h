#ifndef PAGEBLOCARTICLEUTILS_H
#define PAGEBLOCARTICLEUTILS_H

#include <QSet>
#include <QString>

/**
 * Shared article-card rendering utilities used by PageBlocHubGrid and
 * PageBlocConditionList so both produce identical card HTML/CSS from
 * the same logic.
 */
namespace ArticleCardUtils {

static constexpr const char *HUB_GRID_CSS_ID = "page-bloc-hub-grid";
static constexpr const char *HUB_GRID_JS_ID  = "page-bloc-hub-grid";

/**
 * Title-case the last segment of a permalink (strips leading '/' and
 * trailing '.html', replaces '-' with space, capitalises each word).
 */
QString permalinkToTitle(const QString &permalink);

/**
 * Extract the text of the first [TITLE level="1"]...[/TITLE] shortcode
 * from an article body.  Returns empty string when not found; callers
 * should fall back to permalinkToTitle().
 */
QString extractH1Title(const QString &text);

/**
 * Build a plain-text excerpt from shortcode-marked article body text.
 * Stops after maxSentences sentences OR once totalChars >= targetChars,
 * whichever comes first (targetChars=0 disables the character threshold).
 */
QString extractExcerpt(const QString &rawText,
                        qsizetype      maxSentences,
                        qsizetype      targetChars = 0);

/**
 * Emit the hub-card CSS into css once per page (guarded by cssDoneIds).
 * Uses primaryColor for the left-border accent and hover title colour.
 */
void addHubCardCss(QString        &css,
                   QSet<QString>  &cssDoneIds,
                   const QString  &primaryColor);

/**
 * Emit one .hub-card article element into html.
 * href must be an absolute path (with leading '/').
 */
void renderHubCard(QString        &html,
                   const QString  &href,
                   const QString  &title,
                   const QString  &excerpt);

} // namespace ArticleCardUtils

#endif // PAGEBLOCARTICLEUTILS_H
