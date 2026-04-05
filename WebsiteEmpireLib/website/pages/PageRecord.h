#ifndef PAGERECORD_H
#define PAGERECORD_H

#include <QString>

/**
 * Value struct representing a page row from the pages table.
 *
 * id            — auto-incremented primary key; 0 = not yet persisted
 * typeId        — stable page type identifier (e.g. "article")
 * permalink     — URL path stored without domain (e.g. "/my-article.html")
 * lang          — BCP-47 language tag (e.g. "en", "fr")
 * createdAt     — ISO 8601 UTC timestamp
 * updatedAt     — ISO 8601 UTC timestamp; bumped on every saveData() call
 * translatedAt  — ISO 8601 UTC timestamp set by PageTranslator after AI translation;
 *                 empty for source pages (no translation needed) or for translated
 *                 pages that have not yet been processed by the AI.
 *                 When non-empty: if updatedAt > translatedAt the source has changed
 *                 since the last AI pass and a re-translation may be warranted.
 * sourcePageId  — 0 for root/original pages; the id of the source page for
 *                 AI-translated copies.  Translated pages may only be edited in
 *                 PageEditorDialog once translatedAt is non-empty.
 */
struct PageRecord {
    int     id           = 0;
    QString typeId;
    QString permalink;
    QString lang;
    QString createdAt;
    QString updatedAt;
    QString translatedAt;
    int     sourcePageId = 0;
};

#endif // PAGERECORD_H
