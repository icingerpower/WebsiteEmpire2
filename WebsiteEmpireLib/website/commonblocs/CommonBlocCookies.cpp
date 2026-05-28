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

    if (m_message.isEmpty()) {
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

    const QString &displayMessage = resolve(KEY_MESSAGE,     m_message);
    const QString &displayAccept  = resolve(KEY_BUTTON_TEXT, m_buttonText);
    const QString &displayReject  = resolve(KEY_REJECT_TEXT, m_rejectText);

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
              "display:none;"
              "align-items:center;justify-content:space-between;"
              "flex-wrap:wrap;gap:.75rem 1.5rem;"
              "padding:1rem 2rem;"
              "background:rgba(15,15,25,.96);"
              "backdrop-filter:blur(8px);"
              "-webkit-backdrop-filter:blur(8px);"
              "border-top:2px solid ");
        css += primary;
        css += QStringLiteral(";"
              "z-index:9999;box-sizing:border-box;font-family:");
        css += fontFam;
        css += QStringLiteral("}"
            ".cookies-msg{"
              "margin:0;flex:1;min-width:180px;"
              "font-size:.875rem;line-height:1.5;"
              "color:rgba(255,255,255,.9)}"
            ".cookies-actions{display:flex;gap:.5rem;flex-shrink:0}"
            ".cookies-btn-accept{"
              "background:");
        css += primary;
        css += QStringLiteral(";"
              "color:#fff;border:none;"
              "padding:.5rem 1.25rem;border-radius:4px;"
              "cursor:pointer;font-size:.875rem;font-weight:600;"
              "white-space:nowrap;transition:opacity .15s}"
            ".cookies-btn-accept:hover{opacity:.85}"
            ".cookies-btn-reject{"
              "background:transparent;"
              "color:rgba(255,255,255,.75);"
              "border:1px solid rgba(255,255,255,.3);"
              "padding:.5rem 1.25rem;border-radius:4px;"
              "cursor:pointer;font-size:.875rem;white-space:nowrap;"
              "text-decoration:none;"
              "display:inline-flex;align-items:center;"
              "transition:border-color .15s,color .15s}"
            ".cookies-btn-reject:hover{"
              "border-color:rgba(255,255,255,.65);color:#fff}");
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
              "window.__rejectCookies=function(){"
                "_sc('cookies_accepted','0',30);"
                "var b=document.getElementById('cookies-bar');"
                "if(b){"
                  "b.style.display='none';"
                  "var u=b.dataset.rejectUrl;"
                  "if(u)location.href=u;"
                "}"
              "};"
              "if(_gc('cookies_accepted')===null){"
                "var b=document.getElementById('cookies-bar');"
                "if(b)b.style.display='flex';"
              "}"
            "})();");
    }

    // ---- HTML --------------------------------------------------------------
    html += QStringLiteral("<div id=\"cookies-bar\"");
    if (!m_rejectUrl.isEmpty()) {
        html += QStringLiteral(" data-reject-url=\"");
        html += m_rejectUrl.toHtmlEscaped();
        html += QLatin1Char('"');
    }
    html += QStringLiteral("><p class=\"cookies-msg\">");
    html += displayMessage.toHtmlEscaped();
    html += QStringLiteral("</p><div class=\"cookies-actions\">");

    // Reject — <a> when a URL is set, <button> otherwise
    if (!m_rejectUrl.isEmpty()) {
        html += QStringLiteral("<a class=\"cookies-btn-reject\" href=\"");
        html += m_rejectUrl.toHtmlEscaped();
        html += QStringLiteral("\" onclick=\"__rejectCookies();return false;\">");
    } else {
        html += QStringLiteral("<button class=\"cookies-btn-reject\" onclick=\"__rejectCookies()\">");
    }
    html += displayReject.isEmpty() ? QStringLiteral("Reject") : displayReject.toHtmlEscaped();
    html += m_rejectUrl.isEmpty() ? QStringLiteral("</button>") : QStringLiteral("</a>");

    // Accept
    html += QStringLiteral("<button class=\"cookies-btn-accept\" onclick=\"__acceptCookies()\">");
    html += displayAccept.isEmpty() ? QStringLiteral("Accept") : displayAccept.toHtmlEscaped();
    html += QStringLiteral("</button></div></div>");
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
        {QLatin1String(KEY_REJECT_TEXT), m_rejectText},
        {QLatin1String(KEY_REJECT_URL),  m_rejectUrl},
    };
}

void CommonBlocCookies::fromMap(const QVariantMap &map)
{
    m_message    = map.value(QLatin1String(KEY_MESSAGE)).toString();
    m_buttonText = map.value(QLatin1String(KEY_BUTTON_TEXT)).toString();
    m_rejectText = map.value(QLatin1String(KEY_REJECT_TEXT)).toString();
    m_rejectUrl  = map.value(QLatin1String(KEY_REJECT_URL)).toString();
    m_tr.setSource(QLatin1String(KEY_MESSAGE),     m_message);
    m_tr.setSource(QLatin1String(KEY_BUTTON_TEXT), m_buttonText);
    m_tr.setSource(QLatin1String(KEY_REJECT_TEXT), m_rejectText);
}

void CommonBlocCookies::setMessage(const QString &message)
{
    m_message = message;
    m_tr.setSource(QLatin1String(KEY_MESSAGE), message);
}

const QString &CommonBlocCookies::message() const { return m_message; }

void CommonBlocCookies::setButtonText(const QString &text)
{
    m_buttonText = text;
    m_tr.setSource(QLatin1String(KEY_BUTTON_TEXT), text);
}

const QString &CommonBlocCookies::buttonText() const { return m_buttonText; }

void CommonBlocCookies::setRejectText(const QString &text)
{
    m_rejectText = text;
    m_tr.setSource(QLatin1String(KEY_REJECT_TEXT), text);
}

const QString &CommonBlocCookies::rejectText() const { return m_rejectText; }

void CommonBlocCookies::setRejectUrl(const QString &url) { m_rejectUrl = url; }

const QString &CommonBlocCookies::rejectUrl() const { return m_rejectUrl; }

// ---- Translations ----------------------------------------------------------

QHash<QString, QString> CommonBlocCookies::sourceTexts() const
{
    QHash<QString, QString> result;
    if (!m_message.isEmpty())    { result.insert(QLatin1String(KEY_MESSAGE),     m_message); }
    if (!m_buttonText.isEmpty()) { result.insert(QLatin1String(KEY_BUTTON_TEXT), m_buttonText); }
    if (!m_rejectText.isEmpty()) { result.insert(QLatin1String(KEY_REJECT_TEXT), m_rejectText); }
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

QString CommonBlocCookies::translatedText(const QString &fieldId,
                                          const QString &langCode) const
{
    return m_tr.translation(fieldId, langCode);
}
