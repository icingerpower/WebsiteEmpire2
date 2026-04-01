#include "ShortCodeLinkFix.h"

#include "dialogs/ShortCodeLinkDialog.h"

#include <QCoreApplication>

// File-local tr() carries the correct translation context without QObject inheritance.
static QString tr(const char *key)
{
    return QCoreApplication::translate("ShortCodeLinkFix", key);
}

QString ShortCodeLinkFix::getTag() const
{
    return QStringLiteral("LINKFIX");
}

AbstractShortCode::ArgumentDef ShortCodeLinkFix::urlArgumentDef() const
{
    return { ID_URL, /*mandatory=*/true, /*allowedValues=*/{}, /*translatable=*/Translatable::No };
}

QDialog *ShortCodeLinkFix::createEditDialog(QWidget *parent) const
{
    return new ShortCodeLinkDialog(tr("Insert Fixed Link"), parent);
}

QString ShortCodeLinkFix::getButtonName() const
{
    return tr("Link (fixed)");
}

QString ShortCodeLinkFix::getButtonToolTip() const
{
    return tr("Insert a fixed link shortcode");
}

DECLARE_SHORTCODE(ShortCodeLinkFix)
