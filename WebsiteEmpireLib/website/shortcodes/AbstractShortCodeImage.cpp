#include "AbstractShortCodeImage.h"

#include <QRegularExpression>

QList<AbstractShortCode::ArgumentDef> AbstractShortCodeImage::availableArguments() const
{
    return {
        { ID_ID,       /*mandatory=*/true,  /*allowedValues=*/{}, idTranslatable()        },
        { ID_FILENAME, /*mandatory=*/true,  /*allowedValues=*/{}, fileNameTranslatable()  },
        { ID_ALT,      /*mandatory=*/true,  /*allowedValues=*/{}, Translatable::Yes       },
        { ID_WIDTH,    /*mandatory=*/false, /*allowedValues=*/{}, Translatable::No        },
        { ID_HEIGHT,   /*mandatory=*/false, /*allowedValues=*/{}, Translatable::No        },
    };
}

bool AbstractShortCodeImage::isArgumentValueValid(const QString &argId, const QString &value) const
{
    if (argId == QLatin1String(ID_ID)
     || argId == QLatin1String(ID_FILENAME)
     || argId == QLatin1String(ID_ALT)) {
        return !value.trimmed().isEmpty();
    }
    if (argId == QLatin1String(ID_WIDTH) || argId == QLatin1String(ID_HEIGHT)) {
        static const QRegularExpression reDigits(QStringLiteral("^\\d+$"));
        return reDigits.match(value).hasMatch();
    }
    return true;
}

void AbstractShortCodeImage::addCode(QStringView     origContent,
                                     QString        &html,
                                     QString        &css,
                                     QString        &js,
                                     QSet<QString>  &cssDoneIds,
                                     QSet<QString>  &jsDoneIds) const
{
    const ParsedShortCode &parsed  = parseAndValidate(origContent);
    const QString         &alt     = parsed.arguments.value(QStringLiteral("alt"));
    const QString         &width   = parsed.arguments.value(QStringLiteral("width"));
    const QString         &height  = parsed.arguments.value(QStringLiteral("height"));

    // TODO: replace "TODO.webp" with the resolved image path retrieved from
    //       the image lookup class using the id argument once it is available.
    html += QStringLiteral("<img src=\"TODO.webp\" alt=\"");
    html += alt;
    html += QStringLiteral("\"");
    if (!width.isEmpty()) {
        html += QStringLiteral(" width=\"");
        html += width;
        html += QStringLiteral("\"");
    }
    if (!height.isEmpty()) {
        html += QStringLiteral(" height=\"");
        html += height;
        html += QStringLiteral("\"");
    }
    html += QStringLiteral(" />");
    Q_UNUSED(css)
    Q_UNUSED(js)
    Q_UNUSED(cssDoneIds)
    Q_UNUSED(jsDoneIds)
}
