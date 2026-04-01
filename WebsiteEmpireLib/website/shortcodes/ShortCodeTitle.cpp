#include "ShortCodeTitle.h"

#include "dialogs/ShortCodeTitleDialog.h"

#include <QCoreApplication>

// File-local tr() carries the correct translation context without QObject inheritance.
static QString tr(const char *key)
{
    return QCoreApplication::translate("ShortCodeTitle", key);
}

QString ShortCodeTitle::getTag() const
{
    return QStringLiteral("TITLE");
}

QList<AbstractShortCode::ArgumentDef> ShortCodeTitle::availableArguments() const
{
    return {
        { ID_LEVEL, /*mandatory=*/true,
          /*allowedValues=*/QStringList{
              QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
              QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6") },
          /*translatable=*/Translatable::No },
    };
}

bool ShortCodeTitle::isArgumentValueValid(const QString &argId, const QString &value) const
{
    Q_UNUSED(argId)
    Q_UNUSED(value)
    // level: fully covered by allowedValues in validate(); nothing extra needed.
    return true;
}

void ShortCodeTitle::addCode(QStringView     origContent,
                             QString        &html,
                             QString        &css,
                             QString        &js,
                             QSet<QString>  &cssDoneIds,
                             QSet<QString>  &jsDoneIds) const
{
    const ParsedShortCode &parsed = parseAndValidate(origContent);
    const QString &level = parsed.arguments.value(QStringLiteral("level"));
    // Seven direct appends — no intermediate QString allocation (avoids .arg()).
    html += QStringLiteral("<h");
    html += level;
    html += QStringLiteral(">");
    html += parsed.innerContent;
    html += QStringLiteral("</h");
    html += level;
    html += QStringLiteral(">");
    Q_UNUSED(css)
    Q_UNUSED(js)
    Q_UNUSED(cssDoneIds)
    Q_UNUSED(jsDoneIds)
}

QDialog *ShortCodeTitle::createEditDialog(QWidget *parent) const
{
    return new ShortCodeTitleDialog(parent);
}

QString ShortCodeTitle::getTextBegin(const QDialog *dialog) const
{
    const auto *d = qobject_cast<const ShortCodeTitleDialog *>(dialog);
    Q_ASSERT(d != nullptr);
    QString text;
    text += QStringLiteral("[TITLE level=\"");
    text += QString::number(d->level());
    text += QStringLiteral("\"]");
    return text;
}

QString ShortCodeTitle::getTextEnd(const QDialog *dialog) const
{
    Q_UNUSED(dialog)
    return QStringLiteral("[/TITLE]");
}

QString ShortCodeTitle::getButtonName() const
{
    return tr("Title");
}

QString ShortCodeTitle::getButtonToolTip() const
{
    return tr("Insert a heading shortcode");
}

DECLARE_SHORTCODE(ShortCodeTitle)
