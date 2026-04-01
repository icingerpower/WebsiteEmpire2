#ifndef SHORTCODETITLE_H
#define SHORTCODETITLE_H

#include "AbstractShortCode.h"

/**
 * ShortCode [TITLE][/TITLE]
 *
 * Wraps inner content in a heading element at the requested level.
 *
 * Arguments:
 *   level (mandatory, Translatable::No) — heading level; must be "1" through "6"
 *
 * Output:
 *   <h{level}>inner content</h{level}>
 *
 * Example:
 *   [TITLE level="2"]Introduction[/TITLE]   →   <h2>Introduction</h2>
 */
class ShortCodeTitle : public AbstractShortCode
{
public:
    static constexpr const char *ID_LEVEL = "level";

    QString getTag() const override;
    QList<ArgumentDef> availableArguments() const override;

    /**
     * level is fully covered by allowedValues ("1"–"6"); always returns true.
     */
    bool isArgumentValueValid(const QString &argId, const QString &value) const override;

    /**
     * Parses and validates origContent, then appends <hN>innerContent</hN> to html.
     * css, js, cssDoneIds and jsDoneIds are left unchanged (TITLE emits no CSS or JS).
     */
    void addCode(QStringView     origContent,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

    QDialog *createEditDialog(QWidget *parent = nullptr) const override;
    QString getTextBegin(const QDialog *dialog) const override;
    QString getTextEnd(const QDialog *dialog) const override;
    QString getButtonName() const override;
    QString getButtonToolTip() const override;
};

#endif // SHORTCODETITLE_H
