#include "TranslationProtocol.h"

#include <QRegularExpression>

// =============================================================================
// buildPrompt
// =============================================================================

QString TranslationProtocol::buildPrompt(const QList<TranslatableField> &fields,
                                          const QString                  &sourceLang,
                                          const QString                  &targetLang)
{
    QString fieldList;
    for (const TranslatableField &f : std::as_const(fields)) {
        fieldList += QStringLiteral("ID: ") + f.id + QStringLiteral("\n");
        fieldList += f.sourceText;
        fieldList += QStringLiteral("\n---\n");
    }

    return QStringLiteral(
               "Translate the following fields from %1 to %2.\n\n"
               "Each field starts with \"ID: <key>\" followed by the text to translate.\n\n"
               "Rules:\n"
               "- Preserve any [SHORTCODE attr=\"val\"]...[/SHORTCODE] tags verbatim; "
               "translate only the plain text segments between them\n"
               "- Preserve any HTML tags; translate only the text content\n"
               "- Keep proper nouns, brand names, and technical terms appropriate\n"
               "- Never translate shortcode tag names or attribute names\n"
               "- Output the full translation of every field — do not summarise or truncate\n\n"
               "Respond using this EXACT format — one block per field, nothing else:\n\n"
               "===BEGIN <id>===\n"
               "<translated text, may span multiple lines>\n"
               "===END===\n\n"
               "Fields:\n%3")
        .arg(sourceLang, targetLang, fieldList);
}

// =============================================================================
// parseResponse
// =============================================================================

QHash<QString, QString> TranslationProtocol::parseResponse(const QString &response)
{
    QHash<QString, QString> result;

    // Locate each ===BEGIN id=== header.  Using a split-on-header approach rather
    // than a paired BEGIN/END regex so that:
    //  - missing ===END=== (truncated output) still yields partial results
    //  - ===END=== without a preceding newline still works
    //  - Windows \r\n line endings are tolerated
    static const QRegularExpression reBegin(
        QStringLiteral("===BEGIN ([^=\r\n]+)===[ \t]*\r?\n"));

    // Trailing end marker pattern — removed from content after extraction.
    static const QRegularExpression reEnd(
        QStringLiteral("[ \t]*\r?\n?===END===.*$"),
        QRegularExpression::DotMatchesEverythingOption);

    auto it = reBegin.globalMatch(response);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString id = m.captured(1).trimmed();
        if (id.isEmpty()) {
            continue;
        }

        // Content spans from end of this BEGIN line to start of next BEGIN (or end).
        const int contentStart = m.capturedEnd();
        const auto nextBegin   = reBegin.match(response, contentStart);
        const int contentEnd   = nextBegin.hasMatch() ? nextBegin.capturedStart()
                                                       : response.size();

        QString content = response.mid(contentStart, contentEnd - contentStart);

        // Strip trailing ===END=== if present, then trim surrounding whitespace.
        content.remove(reEnd);
        content = content.trimmed();

        if (!content.isEmpty()) {
            result.insert(id, content);
        }
    }
    return result;
}
