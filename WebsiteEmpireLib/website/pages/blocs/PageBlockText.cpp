#include "PageBlockText.h"
#include "website/pages/blocs/widgets/PageBlockTextWidget.h"
#include "website/shortcodes/AbstractShortCode.h"

#include <QRegularExpression>

// =============================================================================
// addCode
// =============================================================================

void PageBlockText::addCode(QStringView     origContent,
                             QString        &html,
                             QString        &css,
                             QString        &js,
                             QSet<QString>  &cssDoneIds,
                             QSet<QString>  &jsDoneIds) const
{
    // Split on one or more blank lines (handles both LF and CRLF line endings).
    static const QRegularExpression reParagraphSep(QStringLiteral("\\r?\\n\\r?\\n+"));

    const auto &content    = origContent.toString();
    const auto &paragraphs = content.split(reParagraphSep);

    for (const QString &para : paragraphs) {
        const auto &trimmedPara = para.trimmed();
        if (trimmedPara.isEmpty()) {
            continue;
        }
        html += QStringLiteral("<p>");
        processText(trimmedPara, html, css, js, cssDoneIds, jsDoneIds);
        html += QStringLiteral("</p>");
    }
}

// =============================================================================
// createEditWidget
// =============================================================================

AbstractPageBlockWidget *PageBlockText::createEditWidget()
{
    return new PageBlockTextWidget;
}

// =============================================================================
// processText  (private static)
// =============================================================================

void PageBlockText::processText(const QString  &text,
                                 QString        &html,
                                 QString        &css,
                                 QString        &js,
                                 QSet<QString>  &cssDoneIds,
                                 QSet<QString>  &jsDoneIds)
{
    // Matches [TAG args]content[/TAG].  The backreference \1 ensures the
    // closing tag name equals the opening one, so nested shortcodes in the
    // content (e.g. [VIDEO][/VIDEO] inside [SPINNABLE][/SPINNABLE]) are
    // consumed as part of the outer match, not as separate top-level matches.
    static const QRegularExpression reShortCode(
        QStringLiteral(R"(\[(\w+)([^\]]*)\](.*?)\[/\1\])"),
        QRegularExpression::DotMatchesEverythingOption);

    int lastEnd = 0;
    QRegularExpressionMatchIterator it = reShortCode.globalMatch(text);
    while (it.hasNext()) {
        const auto &m = it.next();
        const int start = m.capturedStart();

        // Plain text before this shortcode block.
        if (start > lastEnd) {
            html += QStringView(text).mid(lastEnd, start - lastEnd);
        }

        const auto &tag      = m.captured(1);
        const auto &fullText = m.captured(0);
        const AbstractShortCode *sc = AbstractShortCode::forTag(tag);
        if (sc != nullptr) {
            // Collect the shortcode's HTML in a temporary buffer, then
            // recursively expand any shortcodes the handler emitted (e.g. a
            // SPINNABLE that spun out a VIDEO shortcode).
            QString scHtml;
            sc->addCode(fullText, scHtml, css, js, cssDoneIds, jsDoneIds);
            processText(scHtml, html, css, js, cssDoneIds, jsDoneIds);
        } else {
            // Unknown tag: pass through verbatim.
            html += fullText;
        }

        lastEnd = m.capturedEnd();
    }

    // Remaining text after the last shortcode (or all text if none were found).
    if (lastEnd < text.length()) {
        html += QStringView(text).mid(lastEnd);
    }
}
