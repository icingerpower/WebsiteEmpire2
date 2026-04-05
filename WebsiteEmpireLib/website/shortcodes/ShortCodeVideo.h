#ifndef SHORTCODEVIDEO_H
#define SHORTCODEVIDEO_H

#include "AbstractShortCode.h"

/**
 * ShortCode [VIDEO][/VIDEO]
 *
 * Embeds a video via a <video> element.
 *
 * Arguments:
 *   url (mandatory, Translatable::No) — the full video URL
 *
 * Example:
 *   [VIDEO url="https://youtu.be?id=abc123"][/VIDEO]
 */
class ShortCodeVideo : public AbstractShortCode
{
public:
    static constexpr const char *ID_URL = "url";

    QString getTag() const override;
    QList<ArgumentDef> availableArguments() const override;

    /**
     * Validates that the url argument is a non-empty string.
     * Returns true for all other arguments.
     */
    bool isArgumentValueValid(const QString &argId, const QString &value) const override;

    /**
     * Parses and validates origContent, then appends a <video> element to html.
     * css, js, cssDoneIds and jsDoneIds are left unchanged (VIDEO emits no CSS or JS).
     */
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

#endif // SHORTCODEVIDEO_H
