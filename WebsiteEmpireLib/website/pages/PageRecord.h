#ifndef PAGERECORD_H
#define PAGERECORD_H

#include <QString>
#include <QStringList>

/**
 * Value struct representing a page row from the pages table.
 *
 * id                   — auto-incremented primary key; 0 = not yet persisted
 * typeId               — stable page type identifier (e.g. "article")
 * permalink            — URL path stored without domain (e.g. "/my-article.html")
 * lang                 — BCP-47 language tag; the language the page was authored in
 * createdAt            — ISO 8601 UTC timestamp
 * updatedAt            — ISO 8601 UTC timestamp; bumped on every saveData() call
 * translatedAt         — ISO 8601 UTC timestamp set by PageTranslator after AI
 *                        translation (legacy field; kept for backward compatibility).
 * sourcePageId         — legacy field; 0 for root pages (kept for backward compat).
 * generatedAt          — ISO 8601 UTC timestamp set by LauncherGeneration after AI
 *                        content generation; empty until AI generates the content.
 * langCodesToTranslate — BCP-47 codes this page should be translated into,
 *                        set exclusively by the assessment step.  The generator
 *                        throws if a code is listed here but the translation is
 *                        incomplete.  Empty = author language only.
 */
struct PageRecord {
    int         id           = 0;
    QString     typeId;
    QString     permalink;
    QString     lang;
    QString     createdAt;
    QString     updatedAt;
    QString     translatedAt;
    int         sourcePageId = 0;
    QString     generatedAt;
    QStringList langCodesToTranslate; ///< set by assessment step; drives generation
};

#endif // PAGERECORD_H
