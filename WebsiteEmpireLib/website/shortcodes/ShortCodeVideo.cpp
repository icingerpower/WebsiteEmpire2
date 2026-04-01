#include "ShortCodeVideo.h"

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
    Q_UNUSED(css)
    Q_UNUSED(js)
    Q_UNUSED(cssDoneIds)
    Q_UNUSED(jsDoneIds)
}

DECLARE_SHORTCODE(ShortCodeVideo)
