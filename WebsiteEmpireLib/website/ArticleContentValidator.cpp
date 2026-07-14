#include "ArticleContentValidator.h"

#include "website/shortcodes/AbstractShortCode.h"

#include <QRegularExpression>

bool ArticleContentValidator::hasMarkdownArtifact(const QString &text)
{
    // Markdown table header-separator row, e.g. "| --- | --- |" or "|---|---|".
    // Extremely distinctive of a real Markdown table (prose essentially never
    // contains a run of 2+ hyphens between pipe characters), so this has a
    // very low false-positive rate.
    static const QRegularExpression reTableSeparator(
        QStringLiteral(R"(\|\s*:?-{2,}:?\s*\|)"));
    if (reTableSeparator.match(text).hasMatch()) {
        return true;
    }

    // Literal "[NEWLINE]" placeholder — some models emit this instead of an
    // actual newline character when forced to write multi-line content inside
    // a single Markdown table cell. It is not a known shortcode, so it would
    // otherwise leak straight into the rendered HTML as literal text.
    if (text.contains(QStringLiteral("[NEWLINE]"), Qt::CaseInsensitive)) {
        return true;
    }

    return false;
}

bool ArticleContentValidator::isValid(const QString &text)
{
    static const QRegularExpression reSvg(
        QStringLiteral("<svg\\b[^>]*>.*?</svg>"),
        QRegularExpression::DotMatchesEverythingOption
        | QRegularExpression::CaseInsensitiveOption);
    // Require a properly formed first TITLE shortcode: [TITLE level="1"]…[/TITLE]
    // with at least one character between the tags that is not '['.
    // This rejects cases where the AI writes `[TITLE level="1"]` shortcode. as
    // a preamble before the actual article starts.
    static const QRegularExpression reTitleFirst(
        QStringLiteral("^\\[TITLE level=\"1\"\\][^\\[]+\\[/TITLE\\]"),
        QRegularExpression::DotMatchesEverythingOption);

    QString clean = text.trimmed();
    clean.remove(reSvg);
    clean = clean.trimmed();
    if (!clean.startsWith(QLatin1Char('['))) {
        const int pos = clean.indexOf(QStringLiteral("[TITLE level=\"1\"]"));
        if (pos > 0) {
            clean = clean.mid(pos);
        }
    }
    if (!reTitleFirst.match(clean).hasMatch() || clean.size() < 2000) {
        return false;
    }
    if (hasMarkdownArtifact(clean)) {
        return false;
    }
    return AbstractShortCode::validateAllInText(clean).isEmpty();
}
