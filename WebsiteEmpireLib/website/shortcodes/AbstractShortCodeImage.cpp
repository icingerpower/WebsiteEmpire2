#include "AbstractShortCodeImage.h"

#include "dialogs/ShortCodeImageDialog.h"

#include <QRegularExpression>

QString AbstractShortCodeImage::normalizedFileName(const QString &fileName)
{
    const QString trimmed = fileName.trimmed();
    const int lastSlash = trimmed.lastIndexOf(QLatin1Char('/'));
    return lastSlash >= 0 ? trimmed.mid(lastSlash + 1) : trimmed;
}

QList<AbstractShortCode::ArgumentDef> AbstractShortCodeImage::availableArguments() const
{
    return {
        { ID_ID,       /*mandatory=*/true,  /*allowedValues=*/{}, idTranslatable()        },
        { ID_FILENAME, /*mandatory=*/true,  /*allowedValues=*/{}, fileNameTranslatable()  },
        { ID_ALT,      /*mandatory=*/true,  /*allowedValues=*/{}, Translatable::Yes       },
        { ID_WIDTH,    /*mandatory=*/false, /*allowedValues=*/{}, Translatable::No        },
        { ID_HEIGHT,   /*mandatory=*/false, /*allowedValues=*/{}, Translatable::No        },
        { ID_CAPTION,  /*mandatory=*/false, /*allowedValues=*/{}, Translatable::Yes       },
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
                                     AbstractEngine &engine,
                                     int             websiteIndex,
                                     QString        &html,
                                     QString        &css,
                                     QString        &js,
                                     QSet<QString>  &cssDoneIds,
                                     QSet<QString>  &jsDoneIds) const
{
    const ParsedShortCode &parsed   = parseAndValidate(origContent);
    const QString         &fileName = parsed.arguments.value(QLatin1String(ID_FILENAME));
    const QString         &alt      = parsed.arguments.value(QLatin1String(ID_ALT));
    const QString         &width    = parsed.arguments.value(QLatin1String(ID_WIDTH));
    const QString         &height   = parsed.arguments.value(QLatin1String(ID_HEIGHT));
    const QString         &caption  = parsed.arguments.value(QLatin1String(ID_CAPTION));

    const bool hasCaption = !caption.trimmed().isEmpty();
    if (hasCaption) {
        html += QStringLiteral("<figure>");
    }

    // Plain <img> — the page-level lightbox (AbstractPageType) handles zoom for
    // all .page-content img[src] elements automatically.
    // Normalised so an AI-written "/images/foo.jpg" can never emit
    // src="//images/foo.jpg" — see normalizedFileName().
    html += QStringLiteral("<img src=\"/");
    html += normalizedFileName(fileName);
    html += QStringLiteral("\" alt=\"");
    html += alt;
    html += QStringLiteral("\" data-pin-description=\"");
    html += (hasCaption ? caption : alt).toHtmlEscaped();
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
    html += QStringLiteral(" loading=\"lazy\">");

    if (hasCaption) {
        html += QStringLiteral("<figcaption>");
        html += caption;
        html += QStringLiteral("</figcaption></figure>");
    }

    // Pinterest "Save" hover-button widget — loaded once per page no matter
    // how many images use this shortcode. Injected via a dynamically created
    // <script> element rather than appending a literal <script> tag to `js`,
    // because callers inline the whole `js` accumulator into one page-level
    // <script>...</script> block; a literal "</script>" substring in there
    // would close that block early.
    if (!jsDoneIds.contains(QStringLiteral("pinterest_pinit"))) {
        jsDoneIds.insert(QStringLiteral("pinterest_pinit"));
        js += QStringLiteral(
            "(function(){"
                "var s=document.createElement('script');"
                "s.async=true;s.defer=true;"
                "s.src='https://assets.pinterest.com/js/pinit.js';"
                "document.body.appendChild(s);"
            "})();");
    }

    Q_UNUSED(engine)
    Q_UNUSED(websiteIndex)
    Q_UNUSED(css)
    Q_UNUSED(cssDoneIds)
}

QString AbstractShortCodeImage::getTextBegin(const QDialog *dialog) const
{
    const auto *d = qobject_cast<const ShortCodeImageDialog *>(dialog);
    Q_ASSERT(d != nullptr);
    QString text;
    text += QStringLiteral("[");
    text += getTag();
    text += QStringLiteral(" id=\"");
    text += d->id();
    text += QStringLiteral("\" fileName=\"");
    text += d->fileName();
    text += QStringLiteral("\" alt=\"");
    text += d->alt();
    text += QStringLiteral("\"");
    if (d->imageWidth() > 0) {
        text += QStringLiteral(" width=\"");
        text += QString::number(d->imageWidth());
        text += QStringLiteral("\"");
    }
    if (d->imageHeight() > 0) {
        text += QStringLiteral(" height=\"");
        text += QString::number(d->imageHeight());
        text += QStringLiteral("\"");
    }
    text += QStringLiteral("]");
    return text;
}

QString AbstractShortCodeImage::getTextEnd(const QDialog *dialog) const
{
    Q_UNUSED(dialog)
    QString text;
    text += QStringLiteral("[/");
    text += getTag();
    text += QStringLiteral("]");
    return text;
}
