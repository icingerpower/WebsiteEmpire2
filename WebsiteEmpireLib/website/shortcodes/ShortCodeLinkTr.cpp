#include "ShortCodeLinkTr.h"

#include "dialogs/ShortCodeLinkDialog.h"

#include <QCoreApplication>

// File-local tr() carries the correct translation context without QObject inheritance.
static QString tr(const char *key)
{
    return QCoreApplication::translate("ShortCodeLinkTr", key);
}

QString ShortCodeLinkTr::getTag() const
{
    return QStringLiteral("LINKTR");
}

AbstractShortCode::ArgumentDef ShortCodeLinkTr::urlArgumentDef() const
{
    return { ID_URL, /*mandatory=*/true, /*allowedValues=*/{}, /*translatable=*/Translatable::Optional };
}

QDialog *ShortCodeLinkTr::createEditDialog(QWidget *parent) const
{
    return new ShortCodeLinkDialog(tr("Insert Translatable Link"), parent);
}

QString ShortCodeLinkTr::getButtonName() const
{
    return tr("Link (translatable)");
}

QString ShortCodeLinkTr::getButtonToolTip() const
{
    return tr("Insert a translatable link shortcode");
}

DECLARE_SHORTCODE(ShortCodeLinkTr)
