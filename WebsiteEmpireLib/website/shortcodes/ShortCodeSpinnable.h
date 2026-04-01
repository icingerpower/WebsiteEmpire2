#ifndef SHORTCODESPINNABLE_H
#define SHORTCODESPINNABLE_H

#include "AbstractShortCode.h"

class QRandomGenerator;

/**
 * ShortCode [SPINNABLE][/SPINNABLE]
 *
 * Spins the inner content by randomly selecting one alternative from each
 * {option1|option2|...} group.  Groups may be nested arbitrarily.
 *
 * Arguments:
 *   id     (mandatory, Translatable::No) — non-negative integer used as the RNG
 *                                          seed when random=false, ensuring
 *                                          reproducible output for a given id.
 *   random (optional,  Translatable::No) — "true" | "false" (default "false").
 *                                          When "true" the RNG is seeded securely
 *                                          and results vary across calls.
 *
 * Inner content rules:
 *   - Must contain at least one {option1|option2} spin group.
 *   - Every {group} must have at least one '|' separator.
 *   - Braces must be balanced.
 *   - Shortcode tags ([TAG][/TAG]) in the inner content are passed through
 *     verbatim — spinning only processes '{' and '}' characters.
 *
 * Validation uses QRegularExpression for the "at least one spin group" check and
 * a stack-based pass for balanced braces and per-group pipe presence.
 * Violations raise ExceptionWithTitleText.
 *
 * Example:
 *   [SPINNABLE id="7"]{Hello|Hi|Hey} {world|there}![/SPINNABLE]
 */
class ShortCodeSpinnable : public AbstractShortCode
{
public:
    static constexpr const char *ID_ID     = "id";
    static constexpr const char *ID_RANDOM = "random";

    QString getTag() const override;
    QList<ArgumentDef> availableArguments() const override;

    /**
     * For id:     value must match ^\d+$ (non-negative integer digits only).
     * For random: covered entirely by allowedValues; always returns true here.
     */
    bool isArgumentValueValid(const QString &argId, const QString &value) const override;

    /**
     * Validates inner content spinning syntax, then spins it and appends the
     * result to html.  When random=false (default) the RNG is seeded with id
     * for reproducible output.
     * css, js, cssDoneIds and jsDoneIds are left unchanged.
     */
    void addCode(QStringView     origContent,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

private:
    /**
     * Validates spinning syntax:
     *  1. QRegularExpression quick-check that at least one {x|y} group exists.
     *  2. Stack-based pass: balanced braces, every {group} has at least one '|'.
     * Raises ExceptionWithTitleText on the first violation found.
     */
    static void validateSpinnableSyntax(const QString &content);

    /**
     * Recursively resolves all {opt1|opt2|...} groups in text using rng.
     * Text outside spin groups is copied verbatim (preserving shortcode tags, etc.).
     */
    static QString spin(QStringView text, QRandomGenerator &rng);

    /**
     * Splits text on top-level '|' characters (depth-0 only).
     * Pipes inside nested { } groups are ignored.
     * Returns views into text — callers must not outlive text's backing storage.
     */
    static QList<QStringView> splitTopLevel(QStringView text);
};

#endif // SHORTCODESPINNABLE_H
