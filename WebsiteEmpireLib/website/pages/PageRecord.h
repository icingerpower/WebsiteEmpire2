#ifndef PAGERECORD_H
#define PAGERECORD_H

#include <QString>

/**
 * Value struct representing a page row from the pages table.
 *
 * - id          : auto-incremented primary key; 0 = not yet persisted
 * - typeId      : stable page type identifier (e.g. "article")
 * - permalink   : URL path stored without domain (e.g. "/my-article.html")
 * - lang        : BCP 47 language tag (e.g. "en", "fr")
 * - createdAt   : ISO 8601 UTC timestamp
 * - updatedAt   : ISO 8601 UTC timestamp; updated on every saveData() call
 */
struct PageRecord {
    int     id        = 0;
    QString typeId;
    QString permalink;
    QString lang;
    QString createdAt;
    QString updatedAt;
};

#endif // PAGERECORD_H
