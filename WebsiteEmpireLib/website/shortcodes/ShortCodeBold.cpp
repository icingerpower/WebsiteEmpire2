#include "ShortCodeBold.h"

#include "dialogs/ShortCodeInlineDialog.h"

#include <QCoreApplication>

static QString tr(const char *key)
{
    return QCoreApplication::translate("ShortCodeBold", key);
}

QString ShortCodeBold::getTag() const
{
    return QStringLiteral("BOLD");
}

QList<AbstractShortCode::ArgumentDef> ShortCodeBold::availableArguments() const
{
    return {};
}

bool ShortCodeBold::isArgumentValueValid(const QString &argId, const QString &value) const
{
    Q_UNUSED(argId)
    Q_UNUSED(value)
    return true;
}

void ShortCodeBold::addCode(QStringView     origContent,
                             AbstractEngine &engine,
                             int             websiteIndex,
                             QString        &html,
                             QString        &css,
                             QString        &js,
                             QSet<QString>  &cssDoneIds,
                             QSet<QString>  &jsDoneIds) const
{
    const ParsedShortCode &parsed = parseAndValidate(origContent);
    html += QStringLiteral("<strong>");
    html += parsed.innerContent;
    html += QStringLiteral("</strong>");
    Q_UNUSED(engine)
    Q_UNUSED(websiteIndex)
    Q_UNUSED(css)
    Q_UNUSED(js)
    Q_UNUSED(cssDoneIds)
    Q_UNUSED(jsDoneIds)
}

QDialog *ShortCodeBold::createEditDialog(QWidget *parent) const
{
    auto *dlg = new ShortCodeInlineDialog(parent);
    dlg->setWindowTitle(tr("Bold"));
    return dlg;
}

QString ShortCodeBold::getTextBegin(const QDialog *dialog) const
{
    Q_UNUSED(dialog)
    return QStringLiteral("[BOLD]");
}

QString ShortCodeBold::getTextEnd(const QDialog *dialog) const
{
    Q_UNUSED(dialog)
    return QStringLiteral("[/BOLD]");
}

QString ShortCodeBold::getButtonName() const
{
    return tr("Bold");
}

QString ShortCodeBold::getButtonToolTip() const
{
    return tr("Wrap selected text in bold");
}

DECLARE_SHORTCODE(ShortCodeBold)
