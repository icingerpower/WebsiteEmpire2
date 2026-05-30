#include "CommonBlocSearch.h"
#include "website/commonblocs/widgets/WidgetCommonBlocSearch.h"
#include "website/AbstractEngine.h"
#include "website/theme/AbstractTheme.h"

#include <QCoreApplication>
#include <QSet>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>

QString CommonBlocSearch::getId() const
{
    return QStringLiteral("search");
}

QString CommonBlocSearch::getName() const
{
    return QCoreApplication::translate("CommonBlocSearch", "Search Bar");
}

QList<AbstractCommonBloc::ScopeType> CommonBlocSearch::supportedScopes() const
{
    return {ScopeType::Global, ScopeType::PerTheme, ScopeType::PerDomain};
}

void CommonBlocSearch::addCode(QStringView     origContent,
                                AbstractEngine &engine,
                                int             websiteIndex,
                                QString        &html,
                                QString        &css,
                                QString        &js,
                                QSet<QString>  &cssDoneIds,
                                QSet<QString>  &jsDoneIds) const
{
    Q_UNUSED(origContent)
    Q_UNUSED(js)
    Q_UNUSED(jsDoneIds)

    if (m_action.isEmpty()) {
        return;
    }

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

    const QString &displayPlaceholder = resolve(KEY_PLACEHOLDER, m_placeholder);
    const QString primaryColor = theme ? theme->primaryColor() : QStringLiteral("#1a73e8");

    if (!cssDoneIds.contains(QStringLiteral("search"))) {
        cssDoneIds.insert(QStringLiteral("search"));
        css += QStringLiteral(
            ".site-search{"
              "display:flex;justify-content:center;"
              "padding:.5rem 1rem;"
              "background:var(--search-primary,#1a73e8)}"
            ".site-search form{"
              "display:flex;width:100%;max-width:600px}"
            ".site-search input[type=search]{"
              "flex:1;padding:.45rem .75rem;"
              "border:none;border-radius:4px 0 0 4px;"
              "font-size:.95rem;outline:none;"
              "background:#fff;color:#1f2937}"
            ".site-search input[type=search]:focus{"
              "box-shadow:0 0 0 2px rgba(255,255,255,.5)}"
            ".site-search button{"
              "padding:.45rem .9rem;"
              "background:rgba(0,0,0,.2);"
              "color:#fff;border:none;"
              "border-radius:0 4px 4px 0;cursor:pointer;font-size:.95rem}"
            ".site-search button:hover{background:rgba(0,0,0,.35)}");
    }

    // Split the action URL so pre-existing query params (e.g. sitesearch=domain.com)
    // are emitted as hidden inputs — GET forms drop the action's query string on submit.
    const QUrl actionUrl(m_action);
    const QString baseAction = actionUrl.toString(QUrl::RemoveQuery);
    const QList<QPair<QString, QString>> queryItems = QUrlQuery(actionUrl).queryItems();

    html += QStringLiteral("<div class=\"site-search\" style=\"--search-primary:");
    html += primaryColor;
    html += QStringLiteral("\"><form action=\"");
    html += baseAction.toHtmlEscaped();
    html += QStringLiteral("\" method=\"get\" role=\"search\">");
    for (const auto &item : queryItems) {
        html += QStringLiteral("<input type=\"hidden\" name=\"");
        html += item.first.toHtmlEscaped();
        html += QStringLiteral("\" value=\"");
        // For non-source languages, append /{lang} to sitesearch so Google
        // restricts results to the language-specific path (e.g. biomarky.com/de).
        QString value = item.second;
        if (item.first == QStringLiteral("sitesearch") && !lang.isEmpty() && lang != sourceLang) {
            value += QLatin1Char('/') + lang;
        }
        html += value.toHtmlEscaped();
        html += QStringLiteral("\">");
    }
    html += QStringLiteral("<input type=\"search\" name=\"q\"");
    if (!displayPlaceholder.isEmpty()) {
        html += QStringLiteral(" placeholder=\"");
        html += displayPlaceholder.toHtmlEscaped();
        html += QStringLiteral("\"");
    }
    html += QStringLiteral(" aria-label=\"Search\">");
    html += QStringLiteral(
        "<button type=\"submit\" aria-label=\"Search\">"
        "<svg viewBox=\"0 0 20 20\" width=\"16\" height=\"16\""
        " fill=\"currentColor\" aria-hidden=\"true\">"
        "<path d=\"M12.9 14.32a8 8 0 1 1 1.41-1.41l5.35 5.33-1.42 1.42-5.33-5.34z"
        "M8 14A6 6 0 1 0 8 2a6 6 0 0 0 0 12z\"/>"
        "</svg>"
        "</button>");
    html += QStringLiteral("</form></div>");
}

AbstractCommonBlocWidget *CommonBlocSearch::createEditWidget()
{
    return new WidgetCommonBlocSearch();
}

QVariantMap CommonBlocSearch::toMap() const
{
    return {
        {QLatin1String(KEY_ACTION),      m_action},
        {QLatin1String(KEY_PLACEHOLDER), m_placeholder},
    };
}

void CommonBlocSearch::fromMap(const QVariantMap &map)
{
    m_action      = map.value(QLatin1String(KEY_ACTION)).toString();
    m_placeholder = map.value(QLatin1String(KEY_PLACEHOLDER)).toString();
    m_tr.setSource(QLatin1String(KEY_PLACEHOLDER), m_placeholder);
}

void CommonBlocSearch::setAction(const QString &action)
{
    m_action = action;
}

const QString &CommonBlocSearch::action() const { return m_action; }

void CommonBlocSearch::setPlaceholder(const QString &placeholder)
{
    m_placeholder = placeholder;
    m_tr.setSource(QLatin1String(KEY_PLACEHOLDER), placeholder);
}

const QString &CommonBlocSearch::placeholder() const { return m_placeholder; }

QHash<QString, QString> CommonBlocSearch::sourceTexts() const
{
    QHash<QString, QString> result;
    if (!m_placeholder.isEmpty()) {
        result.insert(QLatin1String(KEY_PLACEHOLDER), m_placeholder);
    }
    return result;
}

void CommonBlocSearch::setTranslation(const QString &fieldId,
                                       const QString &langCode,
                                       const QString &translatedText)
{
    m_tr.setTranslation(fieldId, langCode, translatedText);
}

QStringList CommonBlocSearch::missingTranslations(const QString &langCode,
                                                   const QString &sourceLangCode) const
{
    if (langCode.isEmpty() || langCode == sourceLangCode) {
        return {};
    }
    return m_tr.missingFields(langCode);
}

void CommonBlocSearch::saveTranslations(QSettings &settings)
{
    m_tr.saveToSettings(settings);
}

void CommonBlocSearch::loadTranslations(QSettings &settings)
{
    m_tr.loadFromSettings(settings);
}

QString CommonBlocSearch::translatedText(const QString &fieldId,
                                          const QString &langCode) const
{
    return m_tr.translation(fieldId, langCode);
}
