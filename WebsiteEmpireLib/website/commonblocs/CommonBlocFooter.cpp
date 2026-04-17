#include "CommonBlocFooter.h"
#include "website/commonblocs/widgets/WidgetCommonBlocFooter.h"
#include "website/AbstractEngine.h"
#include "website/theme/AbstractTheme.h"

#include <QCoreApplication>
#include <QSet>
#include <QSettings>

QString CommonBlocFooter::getId() const
{
    return QStringLiteral("footer");
}

QString CommonBlocFooter::getName() const
{
    return QCoreApplication::translate("CommonBlocFooter", "Footer");
}

QList<AbstractCommonBloc::ScopeType> CommonBlocFooter::supportedScopes() const
{
    return {ScopeType::Global, ScopeType::PerTheme, ScopeType::PerDomain};
}

void CommonBlocFooter::addCode(QStringView     origContent,
                                AbstractEngine &engine,
                                int             websiteIndex,
                                QString        &html,
                                QString        &css,
                                QString        &js,
                                QSet<QString>  &cssDoneIds,
                                QSet<QString>  &jsDoneIds) const
{
    Q_UNUSED(origContent) // common blocs are not driven by source text
    Q_UNUSED(js)
    Q_UNUSED(jsDoneIds)

    if (m_text.isEmpty()) {
        return;
    }

    // Resolve translated text
    const QString lang = engine.getLangCode(websiteIndex);
    AbstractTheme *theme = engine.getActiveTheme();
    const QString sourceLang = theme ? theme->sourceLangCode() : QString();
    const bool useTranslation = !lang.isEmpty() && lang != sourceLang;

    const QString &displayText = useTranslation
        ? m_tr.translation(QLatin1String(KEY_TEXT), lang)
        : m_text;
    const QString &resolvedText = displayText.isEmpty() ? m_text : displayText;

    if (!cssDoneIds.contains(QStringLiteral("site_footer"))) {
        cssDoneIds.insert(QStringLiteral("site_footer"));

        QString primary  = QStringLiteral("#1a73e8");
        QString fontFam  = QStringLiteral("sans-serif");

        if (theme) {
            primary  = theme->primaryColor();
            fontFam  = theme->fontFamily();
        }

        css += QStringLiteral(".site-footer{background-color:");
        css += primary;
        css += QStringLiteral(";font-family:");
        css += fontFam;
        css += QStringLiteral(";padding:1rem 2rem;margin:0;text-align:center}");
        css += QStringLiteral(".site-footer-text{color:rgba(255,255,255,0.8);margin:0;font-size:0.9em}");
    }

    html += QStringLiteral("<footer class=\"site-footer\">");
    html += QStringLiteral("<p class=\"site-footer-text\">");
    html += resolvedText.toHtmlEscaped();
    html += QStringLiteral("</p>");
    html += QStringLiteral("</footer>");
}

AbstractCommonBlocWidget *CommonBlocFooter::createEditWidget()
{
    return new WidgetCommonBlocFooter();
}

QVariantMap CommonBlocFooter::toMap() const
{
    return {{QLatin1String(KEY_TEXT), m_text}};
}

void CommonBlocFooter::fromMap(const QVariantMap &map)
{
    m_text = map.value(QLatin1String(KEY_TEXT)).toString();
    m_tr.setSource(QLatin1String(KEY_TEXT), m_text);
}

void CommonBlocFooter::setText(const QString &text)
{
    m_text = text;
    m_tr.setSource(QLatin1String(KEY_TEXT), text);
}

const QString &CommonBlocFooter::text() const
{
    return m_text;
}

QHash<QString, QString> CommonBlocFooter::sourceTexts() const
{
    QHash<QString, QString> result;
    if (!m_text.isEmpty()) {
        result.insert(QLatin1String(KEY_TEXT), m_text);
    }
    return result;
}

void CommonBlocFooter::setTranslation(const QString &fieldId,
                                      const QString &langCode,
                                      const QString &translatedText)
{
    m_tr.setTranslation(fieldId, langCode, translatedText);
}

QStringList CommonBlocFooter::missingTranslations(const QString &langCode,
                                                  const QString &sourceLangCode) const
{
    if (langCode.isEmpty() || langCode == sourceLangCode) {
        return {};
    }
    return m_tr.missingFields(langCode);
}

void CommonBlocFooter::saveTranslations(QSettings &settings)
{
    m_tr.saveToSettings(settings);
}

void CommonBlocFooter::loadTranslations(QSettings &settings)
{
    m_tr.loadFromSettings(settings);
}

QString CommonBlocFooter::translatedText(const QString &fieldId,
                                         const QString &langCode) const
{
    return m_tr.translation(fieldId, langCode);
}
