#include "ShortCodeItalic.h"

#include "dialogs/ShortCodeInlineDialog.h"

#include <QCoreApplication>

static QString tr(const char *key)
{
    return QCoreApplication::translate("ShortCodeItalic", key);
}

QString ShortCodeItalic::getTag() const
{
    return QStringLiteral("ITALIC");
}

QList<AbstractShortCode::ArgumentDef> ShortCodeItalic::availableArguments() const
{
    return {};
}

bool ShortCodeItalic::isArgumentValueValid(const QString &argId, const QString &value) const
{
    Q_UNUSED(argId)
    Q_UNUSED(value)
    return true;
}

void ShortCodeItalic::addCode(QStringView     origContent,
                               AbstractEngine &engine,
                               int             websiteIndex,
                               QString        &html,
                               QString        &css,
                               QString        &js,
                               QSet<QString>  &cssDoneIds,
                               QSet<QString>  &jsDoneIds) const
{
    const ParsedShortCode &parsed = parseAndValidate(origContent);
    html += QStringLiteral("<em>");
    html += parsed.innerContent;
    html += QStringLiteral("</em>");
    Q_UNUSED(engine)
    Q_UNUSED(websiteIndex)
    Q_UNUSED(css)
    Q_UNUSED(js)
    Q_UNUSED(cssDoneIds)
    Q_UNUSED(jsDoneIds)
}

QDialog *ShortCodeItalic::createEditDialog(QWidget *parent) const
{
    auto *dlg = new ShortCodeInlineDialog(parent);
    dlg->setWindowTitle(tr("Italic"));
    return dlg;
}

QString ShortCodeItalic::getTextBegin(const QDialog *dialog) const
{
    Q_UNUSED(dialog)
    return QStringLiteral("[ITALIC]");
}

QString ShortCodeItalic::getTextEnd(const QDialog *dialog) const
{
    Q_UNUSED(dialog)
    return QStringLiteral("[/ITALIC]");
}

QString ShortCodeItalic::getButtonName() const
{
    return tr("Italic");
}

QString ShortCodeItalic::getButtonToolTip() const
{
    return tr("Wrap selected text in italic");
}

DECLARE_SHORTCODE(ShortCodeItalic)
