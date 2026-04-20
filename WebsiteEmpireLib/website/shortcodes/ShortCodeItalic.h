#ifndef SHORTCODEITALIC_H
#define SHORTCODEITALIC_H

#include "AbstractShortCode.h"

/**
 * ShortCode [ITALIC][/ITALIC]
 *
 * Wraps inner content in an <em> element.
 *
 * No arguments.
 *
 * Example:
 *   [ITALIC]emphasized text[/ITALIC]   →   <em>emphasized text</em>
 */
class ShortCodeItalic : public AbstractShortCode
{
public:
    QString getTag() const override;
    QList<ArgumentDef> availableArguments() const override;
    bool isArgumentValueValid(const QString &argId, const QString &value) const override;

    void addCode(QStringView     origContent,
                 AbstractEngine &engine,
                 int             websiteIndex,
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

#endif // SHORTCODEITALIC_H
