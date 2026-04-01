#include "AbstractShortCode.h"

#include "ExceptionWithTitleText.h"

#include <QCoreApplication>
#include <QRegularExpression>

// File-local tr() carries the correct translation context without QObject inheritance.
static QString tr(const char *key)
{
    return QCoreApplication::translate("AbstractShortCode", key);
}

// =============================================================================
// Registry
// =============================================================================

static QHash<QString, const AbstractShortCode *> s_shortcodes;

const QHash<QString, const AbstractShortCode *> &AbstractShortCode::ALL_SHORTCODES()
{
    return s_shortcodes;
}

AbstractShortCode::Recorder::Recorder(const AbstractShortCode *shortcode)
{
    Q_ASSERT(!s_shortcodes.contains(shortcode->getTag()));
    s_shortcodes.insert(shortcode->getTag(), shortcode);
}

// =============================================================================
// Factory
// =============================================================================

const AbstractShortCode *AbstractShortCode::forTag(QStringView tag)
{
    return s_shortcodes.value(tag.toString(), nullptr);
}

// =============================================================================
// Parsing  (protected)
// =============================================================================

AbstractShortCode::ParsedShortCode AbstractShortCode::parse(QStringView text)
{
    // Matches [TAG args]content[/TAG] — DotMatchesEverything for multi-line content.
    // The backreference \1 in \[/\1\] forces the closing tag to equal the opening
    // tag, so a non-greedy .*? correctly skips over any inner [/OTHERTAG] fragments
    // (e.g. shortcodes embedded inside the content) and stops only at [/TAG].
    static const QRegularExpression reShortCode(
        R"(\[(\w+)([^\]]*)\](.*?)\[/\1\])",
        QRegularExpression::DotMatchesEverythingOption);

    // Key optimisation: matchView() operates directly on the QStringView,
    // avoiding a copy of the surrounding page buffer.
    const QStringView trimmed = text.trimmed();
    const QRegularExpressionMatch m = reShortCode.matchView(trimmed);
    if (!m.hasMatch()) {
        ExceptionWithTitleText ex(
            tr("ShortCode Parse Error"),
            tr("The text does not match the expected shortcode syntax [TAG ...][/TAG]:\n%1")
                .arg(trimmed.toString()));
        ex.raise();
    }

    const QString openTag = m.captured(1);
    const QString argsStr = m.captured(2);
    const QString content = m.captured(3);

    // Parse key=value pairs inside the args string.
    // The args string has already been extracted from the outer match, so
    // it never contains ']' — the unquoted token pattern (\S+) is safe.
    static const QRegularExpression reArg(
        R"re((\w+)=(?:"([^"]*)"|'([^']*)'|(\S+)))re");

    QHash<QString, QString> arguments;
    QRegularExpressionMatchIterator it = reArg.globalMatch(argsStr);
    while (it.hasNext()) {
        const QRegularExpressionMatch am = it.next();
        const QString key = am.captured(1);

        // captured() returns null QString for a group that did not participate.
        QString value;
        if (!am.captured(2).isNull()) {
            value = am.captured(2); // double-quoted
        } else if (!am.captured(3).isNull()) {
            value = am.captured(3); // single-quoted
        } else {
            value = am.captured(4); // unquoted token
        }

        if (arguments.contains(key)) {
            ExceptionWithTitleText ex(
                tr("ShortCode Parse Error"),
                tr("Duplicate argument '%1' in shortcode [%2].").arg(key, openTag));
            ex.raise();
        }
        arguments.insert(key, value);
    }

    return { openTag, arguments, content };
}

// =============================================================================
// parseAndValidate  (protected)
// =============================================================================

AbstractShortCode::ParsedShortCode AbstractShortCode::parseAndValidate(QStringView text) const
{
    ParsedShortCode parsed = parse(text);
    validate(parsed);
    return parsed;
}

// =============================================================================
// Validation  (private)
// =============================================================================

void AbstractShortCode::validate(const ParsedShortCode &parsed) const
{
    const QList<ArgumentDef> args = availableArguments();

    // 1. Mandatory arguments must be present.
    for (const ArgumentDef &def : std::as_const(args)) {
        if (def.mandatory && !parsed.arguments.contains(def.id)) {
            ExceptionWithTitleText ex(
                tr("ShortCode Validation Error"),
                tr("Mandatory argument '%1' is missing in shortcode [%2].")
                    .arg(def.id, parsed.tag));
            ex.raise();
        }
    }

    // 2. No unknown arguments.
    for (auto it = parsed.arguments.constBegin(); it != parsed.arguments.constEnd(); ++it) {
        const QString &key = it.key();
        const bool known = std::any_of(args.cbegin(), args.cend(),
                                       [&key](const ArgumentDef &d) { return d.id == key; });
        if (!known) {
            ExceptionWithTitleText ex(
                tr("ShortCode Validation Error"),
                tr("Unknown argument '%1' in shortcode [%2].").arg(key, parsed.tag));
            ex.raise();
        }
    }

    // 3. allowedValues constraint + isArgumentValueValid() for each present argument.
    for (const ArgumentDef &def : std::as_const(args)) {
        if (!parsed.arguments.contains(def.id)) {
            continue;
        }
        const QString &value = parsed.arguments.value(def.id);

        if (!def.allowedValues.isEmpty() && !def.allowedValues.contains(value)) {
            ExceptionWithTitleText ex(
                tr("ShortCode Validation Error"),
                tr("Invalid value '%1' for argument '%2' in shortcode [%3]. "
                   "Allowed values: %4.")
                    .arg(value, def.id, parsed.tag, def.allowedValues.join(tr(", "))));
            ex.raise();
        }

        if (!isArgumentValueValid(def.id, value)) {
            ExceptionWithTitleText ex(
                tr("ShortCode Validation Error"),
                tr("Value '%1' is not valid for argument '%2' in shortcode [%3].")
                    .arg(value, def.id, parsed.tag));
            ex.raise();
        }
    }
}
