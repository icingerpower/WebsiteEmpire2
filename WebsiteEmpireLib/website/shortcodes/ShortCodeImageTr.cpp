#include "ShortCodeImageTr.h"

#include "dialogs/ShortCodeImageDialog.h"

#include <QCoreApplication>

// File-local tr() carries the correct translation context without QObject inheritance.
static QString tr(const char *key)
{
    return QCoreApplication::translate("ShortCodeImageTr", key);
}

QString ShortCodeImageTr::getTag() const
{
    return QStringLiteral("IMGTR");
}

AbstractShortCode::Translatable ShortCodeImageTr::idTranslatable() const
{
    return Translatable::Yes;
}

AbstractShortCode::Translatable ShortCodeImageTr::fileNameTranslatable() const
{
    return Translatable::Yes;
}

QDialog *ShortCodeImageTr::createEditDialog(QWidget *parent) const
{
    return new ShortCodeImageDialog(tr("Insert Translatable Image"), parent);
}

QString ShortCodeImageTr::getButtonName() const
{
    return tr("Image (translatable)");
}

QString ShortCodeImageTr::getButtonToolTip() const
{
    return tr("Insert a translatable image shortcode");
}

DECLARE_SHORTCODE(ShortCodeImageTr)
