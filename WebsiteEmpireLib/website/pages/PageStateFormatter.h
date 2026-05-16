#ifndef PAGESTATEFORMATTER_H
#define PAGESTATEFORMATTER_H

#include <QString>
#include <QtGlobal>

/**
 * Display-formatting helpers for PageGenerationState and PageFlag bitmasks.
 *
 * Both functions take raw primitive types so callers do not need to include
 * PageGenerationState.h or PageFlag.h — useful in model/proxy code that
 * receives raw SQL values.
 */
namespace PageStateFormatter {

/**
 * Returns a short human-readable label for a PageGenerationState integer value:
 *   0 (Pending)        → "Pending"
 *   1 (ContentReady)   → "Content"
 *   2 (MainImageReady) → "Image"
 *   3 (Complete)       → "Complete"
 *   unknown            → "?"
 */
QString formatGenerationState(int stateInt);

/**
 * Returns a space-separated string of single-letter badges for each set bit in
 * a PageFlag bitmask.  Badges are always emitted in declaration order:
 *   SocialMedia  → "S"
 *   NeedsRewrite → "R"
 *   NeedsAds     → "A"
 *
 * Example: SocialMedia|NeedsAds → "S A"
 * Returns an empty string when no flags are set.
 */
QString formatFlags(quint32 flags);

} // namespace PageStateFormatter

#endif // PAGESTATEFORMATTER_H
