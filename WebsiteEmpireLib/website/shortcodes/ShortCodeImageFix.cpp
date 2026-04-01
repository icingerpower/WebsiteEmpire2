#include "ShortCodeImageFix.h"

#include "dialogs/ShortCodeImageDialog.h"

#include <QCoreApplication>

// File-local tr() carries the correct translation context without QObject inheritance.
static QString tr(const char *key)
{
    return QCoreApplication::translate("ShortCodeImageFix", key);
}

QString ShortCodeImageFix::getTag() const
{
    return QStringLiteral("IMGFIX");
}

AbstractShortCode::Translatable ShortCodeImageFix::idTranslatable() const
{
    return Translatable::No;
}

AbstractShortCode::Translatable ShortCodeImageFix::fileNameTranslatable() const
{
    return Translatable::Optional;
}

QDialog *ShortCodeImageFix::createEditDialog(QWidget *parent) const
{
    return new ShortCodeImageDialog(tr("Insert Fixed Image"), parent);
}

QString ShortCodeImageFix::getButtonName() const
{
    return tr("Image (fixed)");
}

QString ShortCodeImageFix::getButtonToolTip() const
{
    return tr("Insert a fixed image shortcode");
}

DECLARE_SHORTCODE(ShortCodeImageFix)
