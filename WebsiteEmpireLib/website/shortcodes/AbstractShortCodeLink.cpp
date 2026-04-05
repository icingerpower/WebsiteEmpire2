#include "AbstractShortCodeLink.h"

#include "dialogs/ShortCodeLinkDialog.h"

#include <QStringList>

const QStringList &AbstractShortCodeLink::relValues()
{
    // Single definition of every accepted rel value.
    // DEFAULT_REL must remain the first entry (pre-selected in the dialog).
    static const QStringList values = {
        QStringLiteral("dofollow"),
        QStringLiteral("nofollow"),
        QStringLiteral("sponsored"),
        QStringLiteral("ugc"),
        QStringLiteral("noopener"),
        QStringLiteral("noreferrer"),
        QStringLiteral("noopener noreferrer"),
    };
    return values;
}

QList<AbstractShortCode::ArgumentDef> AbstractShortCodeLink::availableArguments() const
{
    return {
        urlArgumentDef(),
        { ID_REL, /*mandatory=*/false, /*allowedValues=*/{}, /*translatable=*/Translatable::No },
    };
}

bool AbstractShortCodeLink::isArgumentValueValid(const QString &argId, const QString &value) const
{
    if (argId == QLatin1String(ID_URL) || argId == QLatin1String(ID_REL)) {
        return !value.trimmed().isEmpty();
    }
    return true;
}

void AbstractShortCodeLink::addCode(QStringView     origContent,
                                    AbstractEngine &engine,
                                    int             websiteIndex,
                                    QString        &html,
                                    QString        &css,
                                    QString        &js,
                                    QSet<QString>  &cssDoneIds,
                                    QSet<QString>  &jsDoneIds) const
{
    const ParsedShortCode &parsed = parseAndValidate(origContent);
    const QString &url         = parsed.arguments.value(QStringLiteral("url"));
    const QString &relFromArgs = parsed.arguments.value(QStringLiteral("rel"));
    const QString &rel         = relFromArgs.isEmpty() ? QLatin1String(DEFAULT_REL) : relFromArgs;
    // Seven direct appends — no intermediate QString allocation (avoids .arg()).
    html += QStringLiteral("<a href=\"");
    html += url;
    html += QStringLiteral("\" rel=\"");
    html += rel;
    html += QStringLiteral("\">");
    html += parsed.innerContent;
    html += QStringLiteral("</a>");
    Q_UNUSED(engine)
    Q_UNUSED(websiteIndex)
    Q_UNUSED(css)
    Q_UNUSED(js)
    Q_UNUSED(cssDoneIds)
    Q_UNUSED(jsDoneIds)
}

QString AbstractShortCodeLink::getTextBegin(const QDialog *dialog) const
{
    const auto *d = qobject_cast<const ShortCodeLinkDialog *>(dialog);
    Q_ASSERT(d != nullptr);
    QString text;
    text += QStringLiteral("[");
    text += getTag();
    text += QStringLiteral(" url=\"");
    text += d->url();
    text += QStringLiteral("\" rel=\"");
    text += d->rel();
    text += QStringLiteral("\"]");
    return text;
}

QString AbstractShortCodeLink::getTextEnd(const QDialog *dialog) const
{
    Q_UNUSED(dialog)
    QString text;
    text += QStringLiteral("[/");
    text += getTag();
    text += QStringLiteral("]");
    return text;
}
