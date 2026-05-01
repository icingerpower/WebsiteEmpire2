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
};

#endif // TRANSLATIONPROTOCOL_H
