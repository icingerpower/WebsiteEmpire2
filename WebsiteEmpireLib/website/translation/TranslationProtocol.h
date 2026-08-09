#ifndef TRANSLATIONPROTOCOL_H
#define TRANSLATIONPROTOCOL_H

#include "website/WebCodeAdder.h"

#include <QHash>
#include <QList>
#include <QString>

/**
 * Builds the prompt sent to the claude CLI and parses the response.
 *
 * Format
 * ------
 * Claude is instructed to return one block per field:
 *
 *   ===BEGIN <id>===
 *   <translated text, may be multi-line>
 *   ===END===
 *
 * This delimiter format is used instead of JSON because article text often
 * contains shortcode attributes like [TITLE level="1"] whose embedded
 * double-quotes would require JSON-escaping.  Claude frequently returns
 * unescaped quotes, producing invalid JSON that QJsonDocument cannot parse.
 * The delimiter format has no such requirement.
 */
class TranslationProtocol
{
public:
    /**
     * Builds the user message to send to the claude CLI.
     * fields must be non-empty.
     */
    static QString buildPrompt(const QList<TranslatableField> &fields,
                               const QString                  &sourceLang,
                               const QString                  &targetLang);

    /**
     * Parses the claude CLI response produced for a buildPrompt() call.
     * Returns a map of fieldId → translated text.
     * Returns an empty map if no valid blocks are found.
     */
    static QHash<QString, QString> parseResponse(const QString &response);

    /**
     * Cleans a translated value that must be a short, single "name"-like
     * token — a URL slug, symptom name, or category name — never a full
     * sentence or paragraph. AI responses occasionally contain more than the
     * requested value: several candidate phrasings on separate lines (kept
     * "just in case"), or the instructional prompt text echoed back verbatim
     * instead of being translated. Both corrupt any URL slug or display name
     * built directly from the raw value.
     *
     * - When raw contains a newline, keeps only the first non-empty line —
     *   handles the "AI kept two candidate phrasings" failure mode.
     * - Rejects (returns {}) the result when it contains an obvious
     *   prompt-leak marker (case-insensitive: "url slug", "valid url",
     *   "special character", "lowercase words", "this field is"), or when it
     *   is implausibly long relative to sourceText (more than 6x its length,
     *   or more than sourceText's length + 40 chars, whichever is larger —
     *   the additive floor keeps short single-word sources from being
     *   rejected when they legitimately translate into a longer descriptive
     *   phrase).
     *
     * Callers should fall back to the untranslated source when this returns
     * an empty string rather than saving a corrupted value.
     */
    static QString sanitizeShortName(const QString &raw, const QString &sourceText);
};

#endif // TRANSLATIONPROTOCOL_H
