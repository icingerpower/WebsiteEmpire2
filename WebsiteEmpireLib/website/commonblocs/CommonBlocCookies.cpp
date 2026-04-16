#include "CommonBlocCookies.h"
#include "website/commonblocs/widgets/WidgetCommonBlocCookies.h"
#include "website/AbstractEngine.h"
#include "website/theme/AbstractTheme.h"

#include <QCoreApplication>
#include <QSet>
#include <QSettings>

QString CommonBlocCookies::getId() const
{
    return QStringLiteral("cookies");
}

QString CommonBlocCookies::getName() const
{
    return QCoreApplication::translate("CommonBlocCookies", "Cookies consent");
}

QList<AbstractCommonBloc::ScopeType> CommonBlocCookies::supportedScopes() const
{
    return {ScopeType::Global, ScopeType::PerTheme, ScopeType::PerDomain};
}

void CommonBlocCookies::addCode(QStringView     origContent,
                                 AbstractEngine &engine,
                                 int             websiteIndex,
                                 QString        &html,
                                 QString        &css,
                                 QString        &js,
                                 QSet<QString>  &cssDoneIds,
                                 QSet<QString>  &jsDoneIds) const
{
    Q_UNUSED(origContent)

    if (m_message.isEmpty() && m_buttonText.isEmpty()) {
        return;
    }

    // ---- Resolve translated text -------------------------------------------
    const QString lang = engine.getLangCode(websiteIndex);
    AbstractTheme *theme = engine.getActiveTheme();
    const QString sourceLang = theme ? theme->sourceLangCode() : QString();
    const bool useTranslation = !lang.isEmpty() && lang != sourceLang;

    const auto resolve = [&](const char *key, const QString &source) -> const QString & {
        if (!useTranslation) {
            return source;
        }
        const QString &t = m_tr.translation(QLatin1String(key), lang);
        return t.isEmpty() ? source : t;
    };

    const QString &displayMessage    = resolve(KEY_MESSAGE,     m_message);
    const QString &displayButtonText = resolve(KEY_BUTTON_TEXT, m_buttonText);

    // ---- CSS (once per page) -----------------------------------------------
    if (!cssDoneIds.contains(QStringLiteral("cookies_bar"))) {
        cssDoneIds.insert(QStringLiteral("cookies_bar"));

        QString primary = QStringLiteral("#1a73e8");
        QString fontFam = QStringLiteral("sans-serif");
        if (theme) {
            primary = theme->primaryColor();
            fontFam = theme->fontFamily();
        }

        css += QStringLiteral(
            "#cookies-bar{"
              "position:fixed;bottom:0;left:0;right:0;"
              "display:none;"      // JS sets it to flex when consent is missing
              "align-items:center;justify-content:space-between;gap:1rem;"
              "padding:1rem 2rem;"
              "background:#222;color:#fff;"
              "z-index:9999;box-sizing:border-box;"
              "font-family:");
        css += fontFam;
        css += QStringLiteral("}"
            ".cookies-msg{margin:0;flex:1;font-size:.9rem}"
            ".cookies-btn{"
              "background:");
        css += primary;
        css += QStringLiteral(";"
              "color:#fff;border:none;"
              "padding:.5rem 1.25rem;"
              "border-radius:4px;cursor:pointer;"
              "white-space:nowrap;font-size:.9rem}"
            ".cookies-btn:hover{opacity:.85}");
    }

    // ---- JS (once per page) ------------------------------------------------
    if (!jsDoneIds.contains(QStringLiteral("cookies_bar"))) {
        jsDoneIds.insert(QStringLiteral("cookies_bar"));

        js += QStringLiteral(
            "(function(){"
              "function _gc(n){var v=document.cookie.match('(^|;) ?'+n+'=([^;]*)(;|$)');return v?v[2]:null;}"
              "function _sc(n,v,d){var e=new Date();e.setTime(e.getTime()+86400000*d);"
                "document.cookie=n+'='+v+';expires='+e.toUTCString()+';path=/;SameSite=Lax';}"
              "window.__acceptCookies=function(){"
                "_sc('cookies_accepted','1',365);"
                "var b=document.getElementById('cookies-bar');"
                "if(b)b.style.display='none';"
              "};"
              "if(!_gc('cookies_accepted')){"
                "var b=document.getElementById('cookies-bar');"
                "if(b)b.style.display='flex';"
              "}"
            "})();");
    }

    // ---- HTML --------------------------------------------------------------
    html += QStringLiteral("<div id=\"cookies-bar\" class=\"cookies-bar\">");
    if (!displayMessage.isEmpty()) {
        html += QStringLiteral("<p class=\"cookies-msg\">");
        html += displayMessage.toHtmlEscaped();
        html += QStringLiteral("</p>");
    }
    html += QStringLiteral("<button class=\"cookies-btn\" onclick=\"window.__acceptCookies()\">");
    html += displayButtonText.isEmpty()
        ? QStringLiteral("Accept")
        : displayButtonText.toHtmlEscaped();
    html += QStringLiteral("</button>");
    html += QStringLiteral("</div>");
}

AbstractCommonBlocWidget *CommonBlocCookies::createEditWidget()
{
    return new WidgetCommonBlocCookies();
}

QVariantMap CommonBlocCookies::toMap() const
{
    return {
        {QLatin1String(KEY_MESSAGE),     m_message},
        {QLatin1String(KEY_BUTTON_TEXT), m_buttonText},
    };
}

void CommonBlocCookies::fromMap(const QVariantMap &map)
{
    m_message    = map.value(QLatin1String(KEY_MESSAGE)).toString();
    m_buttonText = map.value(QLatin1String(KEY_BUTTON_TEXT)).toString();
    m_tr.setSource(QLatin1String(KEY_MESSAGE),     m_message);
    m_tr.setSource(QLatin1String(KEY_BUTTON_TEXT), m_buttonText);
}

void CommonBlocCookies::setMessage(const QString &message)
{
    m_message = message;
    m_tr.setSource(QLatin1String(KEY_MESSAGE), message);
}

const QString &CommonBlocCookies::message() const
{
    return m_message;
}

void CommonBlocCookies::setButtonText(const QString &text)
{
    m_buttonText = text;
    m_tr.setSource(QLatin1String(KEY_BUTTON_TEXT), text);
}

const QString &CommonBlocCookies::buttonText() const
{
    return m_buttonText;
}

QHash<QString, QString> CommonBlocCookies::sourceTexts() const
{
    QHash<QString, QString> result;
    if (!m_message.isEmpty()) {
        result.insert(QLatin1String(KEY_MESSAGE), m_message);
    }
    if (!m_buttonText.isEmpty()) {
        result.insert(QLatin1String(KEY_BUTTON_TEXT), m_buttonText);
    }
    return result;
}

void CommonBlocCookies::setTranslation(const QString &fieldId,
                                        const QString &langCode,
                                        const QString &translatedText)
{
    m_tr.setTranslation(fieldId, langCode, translatedText);
}

QStringList CommonBlocCookies::missingTranslations(const QString &langCode,
                                                    const QString &sourceLangCode) const
{
    if (langCode.isEmpty() || langCode == sourceLangCode) {
        return {};
    }
    return m_tr.missingFields(langCode);
}

void CommonBlocCookies::saveTranslations(QSettings &settings)
{
    m_tr.saveToSettings(settings);
}

void CommonBlocCookies::loadTranslations(QSettings &settings)
{
    m_tr.loadFromSettings(settings);
}
