#include "PageBlocArticleUtils.h"

#include <QRegularExpression>

namespace ArticleCardUtils {

// =============================================================================
// permalinkToTitle
// =============================================================================

QString permalinkToTitle(const QString &permalink)
{
    QString title = permalink;
    if (title.startsWith(QLatin1Char('/'))) {
        title = title.mid(1);
    }
    if (title.endsWith(QStringLiteral(".html"))) {
        title.chop(5);
    }
    title.replace(QLatin1Char('-'), QLatin1Char(' '));
    bool capitalizeNext = true;
    for (int i = 0; i < title.size(); ++i) {
        if (title.at(i) == QLatin1Char(' ')) {
            capitalizeNext = true;
        } else if (capitalizeNext) {
            title[i] = title.at(i).toUpper();
            capitalizeNext = false;
        }
    }
    return title;
}

// =============================================================================
// extractH1Title
// =============================================================================

QString extractH1Title(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral("\\[TITLE\\b[^\\]]*level=[\"']1[\"'][^\\]]*\\]([^\\[]+)\\[/TITLE\\]"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(text);
    if (m.hasMatch()) {
        return m.captured(1).trimmed();
    }
    return {};
}

// =============================================================================
// extractExcerpt
// =============================================================================

QString extractExcerpt(const QString &rawText,
                        qsizetype      maxSentences,
                        qsizetype      targetChars)
{
    if (rawText.isEmpty()) {
        return {};
    }
    QString plain;
    const auto &paras = rawText.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts);
    for (const QString &para : paras) {
        const QString &trimmed = para.trimmed();
        if (trimmed.startsWith(QLatin1Char('['))) {
            continue;
        }
        QString clean;
        clean.reserve(trimmed.size());
        int depth = 0;
        for (const QChar &ch : trimmed) {
            if (ch == QLatin1Char('[')) {
                ++depth;
            } else if (ch == QLatin1Char(']')) {
                if (depth > 0) {
                    --depth;
                }
            } else if (depth == 0) {
                clean += ch;
            }
        }
        const QString &cleanTrimmed = clean.trimmed();
        if (!cleanTrimmed.isEmpty()) {
            if (!plain.isEmpty()) {
                plain += QLatin1Char(' ');
            }
            plain += cleanTrimmed;
        }
    }
    if (plain.isEmpty()) {
        return {};
    }
    QStringList sentences;
    qsizetype totalChars = 0;
    qsizetype start = 0;
    const qsizetype len = plain.size();
    for (qsizetype i = 0; i < len; ++i) {
        const QChar c = plain.at(i);
        const bool isAscii = c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?');
        const bool isCjk   = c == QChar(0x3002) || c == QChar(0xFF01) || c == QChar(0xFF1F);
        if (!isAscii && !isCjk) {
            continue;
        }
        if (isAscii && i + 1 < len && plain.at(i + 1) != QLatin1Char(' ')) {
            continue;
        }
        const QString sentence = plain.mid(start, i - start + 1).trimmed();
        sentences.append(sentence);
        totalChars += sentence.size();
        start = i + (isAscii ? 2 : 1);
        if (sentences.size() >= maxSentences || (targetChars > 0 && totalChars >= targetChars)) {
            break;
        }
    }
    const bool underLimit = sentences.size() < maxSentences
                            && (targetChars == 0 || totalChars < targetChars);
    if (underLimit && start < len) {
        QString tail = plain.mid(start).trimmed();
        if (!tail.isEmpty()) {
            if (targetChars > 0 && totalChars + tail.size() > targetChars * 2) {
                tail = tail.left(targetChars - totalChars).trimmed()
                       + QStringLiteral("…");
            }
            sentences.append(tail);
        }
    }
    return sentences.join(QStringLiteral(" "));
}

// =============================================================================
// addHubCardCss
// =============================================================================

void addHubCardCss(QString        &css,
                   QSet<QString>  &cssDoneIds,
                   const QString  &primaryColor)
{
    if (cssDoneIds.contains(QLatin1String(HUB_GRID_CSS_ID))) {
        return;
    }
    cssDoneIds.insert(QLatin1String(HUB_GRID_CSS_ID));

    css += QStringLiteral(
        ".hub-grid{"
            "display:grid;"
            "grid-template-columns:repeat(auto-fill,minmax(280px,1fr));"
            "gap:1.5rem;"
            "padding:1rem 0"
        "}"
        ".hub-card{"
            "background:#fff;"
            "border-radius:.75rem;"
            "box-shadow:0 2px 12px rgba(0,0,0,.07);"
            "border-left:4px solid ");
    css += primaryColor;
    css += QStringLiteral(
        ";"
            "padding:1.25rem;"
            "display:flex;"
            "flex-direction:column;"
            "gap:.5rem;"
            "transition:transform .2s,box-shadow .2s"
        "}"
        ".hub-card:hover{"
            "transform:translateY(-3px);"
            "box-shadow:0 6px 20px rgba(0,0,0,.12)"
        "}"
        ".hub-card__link{"
            "display:block;"
            "text-decoration:none;"
            "color:#1a1a1a"
        "}"
        ".hub-card__title{"
            "font-size:1.05rem;"
            "font-weight:600;"
            "margin:0;"
            "line-height:1.4;"
            "color:#1a1a1a;"
            "transition:color .15s"
        "}"
        ".hub-card__link:hover .hub-card__title{color:");
    css += primaryColor;
    css += QStringLiteral(
        "}"
        ".hub-card__excerpt{"
            "font-size:.875rem;"
            "color:#1a1a1a;"
            "margin:0;"
            "line-height:1.6"
        "}");
}

// =============================================================================
// renderHubCard
// =============================================================================

void renderHubCard(QString        &html,
                   const QString  &href,
                   const QString  &title,
                   const QString  &excerpt)
{
    html += QStringLiteral("<article class=\"hub-card\">");
    html += QStringLiteral("<a href=\"");
    html += href;
    html += QStringLiteral("\" class=\"hub-card__link\">");
    html += QStringLiteral("<h3 class=\"hub-card__title\">");
    html += title.toHtmlEscaped();
    html += QStringLiteral("</h3>");
    html += QStringLiteral("</a>");
    if (!excerpt.isEmpty()) {
        html += QStringLiteral("<p class=\"hub-card__excerpt\">");
        html += excerpt.toHtmlEscaped();
        html += QStringLiteral("</p>");
    }
    html += QStringLiteral("</article>");
}

} // namespace ArticleCardUtils
