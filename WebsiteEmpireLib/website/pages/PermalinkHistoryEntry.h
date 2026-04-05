#ifndef PERMALINKHISTORYENTRY_H
#define PERMALINKHISTORYENTRY_H

#include <QString>

/**
 * One row from the permalink_history table.
 *
 * redirectType controls how the static server handles requests for the old URL:
 *   "permanent" — 301 Moved Permanently to the current permalink (default)
 *   "parked"    — 302 Found (temporary redirect to the current permalink)
 *   "deleted"   — 410 Gone (no forwarding; the content was intentionally removed)
 */
struct PermalinkHistoryEntry {
    int     id           = 0;
    int     pageId       = 0;
    QString permalink;
    QString changedAt;
    QString redirectType = QStringLiteral("permanent");
};

#endif // PERMALINKHISTORYENTRY_H
