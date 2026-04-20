#ifndef SHORTCODEBOLD_H
#define SHORTCODEBOLD_H

#include "AbstractShortCode.h"

/**
 * ShortCode [BOLD][/BOLD]
 *
 * Wraps inner content in a <strong> element.
 *
 * No arguments.
 *
 * Example:
 *   [BOLD]important text[/BOLD]   →   <strong>important text</strong>
 */
class ShortCodeBold : public AbstractShortCode
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

#endif // SHORTCODEBOLD_H
