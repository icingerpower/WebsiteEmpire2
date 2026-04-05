#include "ShortCodeSpinnable.h"

#include "dialogs/ShortCodeSpinnableDialog.h"
#include "ExceptionWithTitleText.h"

#include <QCoreApplication>
#include <QRandomGenerator>
#include <QRegularExpression>

// File-local tr() carries the correct translation context without QObject inheritance.
static QString tr(const char *key)
{
    return QCoreApplication::translate("ShortCodeSpinnable", key);
}

// =============================================================================
// Identity & arguments
// =============================================================================

QString ShortCodeSpinnable::getTag() const
{
    return QStringLiteral("SPINNABLE");
}

QList<AbstractShortCode::ArgumentDef> ShortCodeSpinnable::availableArguments() const
{
    return {
        { ID_ID,     /*mandatory=*/true,  /*allowedValues=*/{},
          /*translatable=*/Translatable::No },
        { ID_RANDOM, /*mandatory=*/false,
          /*allowedValues=*/QStringList{ QStringLiteral("true"), QStringLiteral("false") },
          /*translatable=*/Translatable::No },
    };
}

bool ShortCodeSpinnable::isArgumentValueValid(const QString &argId, const QString &value) const
{
    if (argId == QLatin1String(ID_ID)) {
        // id must be a non-negative integer (digits only).
        static const QRegularExpression reDigits(QStringLiteral("^\\d+$"));
        return reDigits.match(value).hasMatch();
    }
    // random: fully covered by allowedValues in validate(); nothing extra needed.
    return true;
}

// =============================================================================
// Spinning helpers (private static)
// =============================================================================

void ShortCodeSpinnable::validateSpinnableSyntax(const QString &content)
{
    // 1. Quick regex check: at least one innermost {x|y} group must exist.
    //    The pattern requires non-brace chars around the pipe so that nested
    //    structures are caught by their innermost non-nested group.
    static const QRegularExpression reHasGroup(QStringLiteral("\\{[^{}]*\\|[^{}]*\\}"));
    if (!reHasGroup.match(content).hasMatch()) {
        ExceptionWithTitleText ex(
            tr("SPINNABLE Syntax Error"),
            tr("Inner content contains no spin group. "
               "At least one {option1|option2} group is required."));
        ex.raise();
    }

    // 2. Stack-based pass: balanced braces, every group has at least one '|'.
    int depth = 0;
    QVector<bool> depthHasPipe;
    depthHasPipe.reserve(8);

    for (const QChar ch : std::as_const(content)) {
        if (ch == QLatin1Char('{')) {
            depth++;
            depthHasPipe.append(false);
        } else if (ch == QLatin1Char('}')) {
            if (depth == 0) {
                ExceptionWithTitleText ex(
                    tr("SPINNABLE Syntax Error"),
                    tr("Unexpected '}' with no matching '{'."));
                ex.raise();
            }
            if (!depthHasPipe.last()) {
                ExceptionWithTitleText ex(
                    tr("SPINNABLE Syntax Error"),
                    tr("Spin group '{...}' has no '|' separator. "
                       "Every {group} must offer at least two alternatives."));
                ex.raise();
            }
            depthHasPipe.removeLast();
            depth--;
        } else if (ch == QLatin1Char('|') && depth > 0) {
            depthHasPipe.last() = true;
        }
    }

    if (depth != 0) {
        ExceptionWithTitleText ex(
            tr("SPINNABLE Syntax Error"),
            tr("%1 opening brace(s) '{' were never closed.").arg(depth));
        ex.raise();
    }
}

QList<QStringView> ShortCodeSpinnable::splitTopLevel(QStringView text)
{
    QList<QStringView> parts;
    int depth = 0;
    int start = 0;
    for (int i = 0; i < text.length(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('{')) {
            depth++;
        } else if (ch == QLatin1Char('}')) {
            depth--;
        } else if (ch == QLatin1Char('|') && depth == 0) {
            parts.append(text.mid(start, i - start));
            start = i + 1;
        }
    }
    parts.append(text.mid(start));
    return parts;
}

QString ShortCodeSpinnable::spin(QStringView text, QRandomGenerator &rng)
{
    QString result;
    result.reserve(text.length());
    int i = 0;
    while (i < text.length()) {
        if (text.at(i) == QLatin1Char('{')) {
            // Find the matching closing brace.
            int depth = 1;
            int start = i + 1;
            int j = start;
            while (j < text.length() && depth > 0) {
                if (text.at(j) == QLatin1Char('{')) {
                    depth++;
                } else if (text.at(j) == QLatin1Char('}')) {
                    depth--;
                }
                j++;
            }
            // text[start .. j-2] is the content between the matched braces.
            const QStringView inner = text.mid(start, j - start - 1);
            const auto &options = splitTopLevel(inner);
            const int choice = static_cast<int>(rng.bounded(static_cast<quint32>(options.size())));
            result += spin(options.at(choice), rng);
            i = j;
        } else {
            result += text.at(i);
            i++;
        }
    }
    return result;
}

// =============================================================================
// addCode
// =============================================================================

void ShortCodeSpinnable::addCode(QStringView     origContent,
                                 AbstractEngine &engine,
                                 int             websiteIndex,
                                 QString        &html,
                                 QString        &css,
                                 QString        &js,
                                 QSet<QString>  &cssDoneIds,
                                 QSet<QString>  &jsDoneIds) const
{
    const ParsedShortCode &parsed    = parseAndValidate(origContent);
    const QString         &idStr     = parsed.arguments.value(QStringLiteral("id"));
    const QString         &randomStr = parsed.arguments.value(QStringLiteral("random"));
    const bool isRandom = (randomStr == QStringLiteral("true"));

    validateSpinnableSyntax(parsed.innerContent);

    const quint64 seed = idStr.toULongLong();
    QRandomGenerator rng = isRandom ? QRandomGenerator::securelySeeded()
                                    : QRandomGenerator(seed);
    html += spin(parsed.innerContent, rng);
    Q_UNUSED(engine)
    Q_UNUSED(websiteIndex)
    Q_UNUSED(css)
    Q_UNUSED(js)
    Q_UNUSED(cssDoneIds)
    Q_UNUSED(jsDoneIds)
}

QDialog *ShortCodeSpinnable::createEditDialog(QWidget *parent) const
{
    return new ShortCodeSpinnableDialog(parent);
}

QString ShortCodeSpinnable::getTextBegin(const QDialog *dialog) const
{
    const auto *d = qobject_cast<const ShortCodeSpinnableDialog *>(dialog);
    Q_ASSERT(d != nullptr);
    QString text;
    text += QStringLiteral("[SPINNABLE id=\"");
    text += QString::number(d->spinnableId());
    text += QStringLiteral("\" random=\"");
    text += d->random() ? QStringLiteral("true") : QStringLiteral("false");
    text += QStringLiteral("\"]");
    return text;
}

QString ShortCodeSpinnable::getTextEnd(const QDialog *dialog) const
{
    Q_UNUSED(dialog)
    return QStringLiteral("[/SPINNABLE]");
}

QString ShortCodeSpinnable::getButtonName() const
{
    return tr("Spinnable");
}

QString ShortCodeSpinnable::getButtonToolTip() const
{
    return tr("Insert a spinnable text shortcode");
}

DECLARE_SHORTCODE(ShortCodeSpinnable)
