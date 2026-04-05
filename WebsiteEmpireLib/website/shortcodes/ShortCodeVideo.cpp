#include "ShortCodeVideo.h"

#include "dialogs/ShortCodeVideoDialog.h"

#include <QCoreApplication>

// File-local tr() carries the correct translation context without QObject inheritance.
static QString tr(const char *key)
{
    return QCoreApplication::translate("ShortCodeVideo", key);
}

QString ShortCodeVideo::getTag() const
{
    return QStringLiteral("VIDEO");
}

QList<AbstractShortCode::ArgumentDef> ShortCodeVideo::availableArguments() const
{
    return {
        { ID_URL, /*mandatory=*/true, /*allowedValues=*/{}, /*translatable=*/Translatable::No },
    };
}

bool ShortCodeVideo::isArgumentValueValid(const QString &argId, const QString &value) const
{
    if (argId == QLatin1String(ID_URL)) {
        return !value.trimmed().isEmpty();
    }
    return true;
}

void ShortCodeVideo::addCode(QStringView     origContent,
                             AbstractEngine &engine,
                             int             websiteIndex,
                             QString        &html,
                             QString        &css,
                             QString        &js,
                             QSet<QString>  &cssDoneIds,
                             QSet<QString>  &jsDoneIds) const
{
    const ParsedShortCode &parsed = parseAndValidate(origContent);
    const QString &url = parsed.arguments.value(QStringLiteral("url"));
    // Three direct appends: no intermediate QString allocation (avoids .arg()).
    html += QStringLiteral("<video src=\"");
    html += url;
    html += QStringLiteral("\" controls></video>");
    Q_UNUSED(engine)
    Q_UNUSED(websiteIndex)
    Q_UNUSED(css)
    Q_UNUSED(js)
    Q_UNUSED(cssDoneIds)
    Q_UNUSED(jsDoneIds)
}

QDialog *ShortCodeVideo::createEditDialog(QWidget *parent) const
{
    return new ShortCodeVideoDialog(parent);
}

QString ShortCodeVideo::getTextBegin(const QDialog *dialog) const
{
    const auto *d = qobject_cast<const ShortCodeVideoDialog *>(dialog);
    Q_ASSERT(d != nullptr);
    QString text;
    text += QStringLiteral("[VIDEO url=\"");
    text += d->url();
    text += QStringLiteral("\"]");
    return text;
}

QString ShortCodeVideo::getTextEnd(const QDialog *dialog) const
{
    Q_UNUSED(dialog)
    return QStringLiteral("[/VIDEO]");
}

QString ShortCodeVideo::getButtonName() const
{
    return tr("Video");
}

QString ShortCodeVideo::getButtonToolTip() const
{
    return tr("Insert a video shortcode");
}

DECLARE_SHORTCODE(ShortCodeVideo)
