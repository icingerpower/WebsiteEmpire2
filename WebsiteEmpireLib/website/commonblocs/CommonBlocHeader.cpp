#include "CommonBlocHeader.h"
#include "website/commonblocs/widgets/WidgetCommonBlocHeader.h"
#include "website/AbstractEngine.h"
#include "website/theme/AbstractTheme.h"

#include <QCoreApplication>
#include <QSet>
#include <QSettings>

QString CommonBlocHeader::getId() const
{
    return QStringLiteral("header");
}

QString CommonBlocHeader::getName() const
{
    return QCoreApplication::translate("CommonBlocHeader", "Header");
}

QList<AbstractCommonBloc::ScopeType> CommonBlocHeader::supportedScopes() const
{
    return {ScopeType::Global, ScopeType::PerTheme, ScopeType::PerDomain};
}

void CommonBlocHeader::addCode(QStringView     origContent,
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

    if (m_title.isEmpty()) {
        return;
    }

    // Resolve translated title / subtitle
    const QString lang = engine.getLangCode(websiteIndex);
    AbstractTheme *theme = engine.getActiveTheme();
    const QString sourceLang = theme ? theme->sourceLangCode() : QString();
    const bool useTranslation = !lang.isEmpty() && lang != sourceLang;

    const QString &displayTitle = useTranslation
        ? m_tr.translation(QLatin1String(KEY_TITLE), lang)
        : m_title;
    const QString &resolvedTitle = displayTitle.isEmpty() ? m_title : displayTitle;

    const QString &displaySubtitle = useTranslation
        ? m_tr.translation(QLatin1String(KEY_SUBTITLE), lang)
        : m_subtitle;
    const QString &resolvedSubtitle = displaySubtitle.isEmpty() ? m_subtitle : displaySubtitle;

    if (!cssDoneIds.contains(QStringLiteral("site_header"))) {
        cssDoneIds.insert(QStringLiteral("site_header"));

        QString primary   = QStringLiteral("#1a73e8");
        QString secondary = QStringLiteral("#fbbc04");
        QString font      = QStringLiteral("sans-serif");

        if (theme) {
            primary   = theme->primaryColor();
            secondary = theme->secondaryColor();
            font      = theme->fontFamily();
        }

        css += QStringLiteral(".site-header{background-color:");
        css += primary;
        css += QStringLiteral(";font-family:");
        css += font;
        css += QStringLiteral(";padding:1.5rem 2rem;margin:0}");
        css += QStringLiteral(".site-title{color:#fff;margin:0;font-size:1.75em;font-weight:bold}");
        css += QStringLiteral(".site-subtitle{color:");
        css += secondary;
        css += QStringLiteral(";margin:0.25rem 0 0;font-size:1.1em;font-weight:normal}");
    }

    html += QStringLiteral("<header class=\"site-header\">");
    html += QStringLiteral("<h1 class=\"site-title\">");
    html += resolvedTitle.toHtmlEscaped();
    html += QStringLiteral("</h1>");
    if (!resolvedSubtitle.isEmpty()) {
        html += QStringLiteral("<p class=\"site-subtitle\">");
        html += resolvedSubtitle.toHtmlEscaped();
        html += QStringLiteral("</p>");
    }
    html += QStringLiteral("</header>");
}

AbstractCommonBlocWidget *CommonBlocHeader::createEditWidget()
{
    return new WidgetCommonBlocHeader();
}

QVariantMap CommonBlocHeader::toMap() const
{
    return {
        {QLatin1String(KEY_TITLE),    m_title},
        {QLatin1String(KEY_SUBTITLE), m_subtitle},
    };
}

void CommonBlocHeader::fromMap(const QVariantMap &map)
{
    m_title    = map.value(QLatin1String(KEY_TITLE)).toString();
    m_subtitle = map.value(QLatin1String(KEY_SUBTITLE)).toString();
    m_tr.setSource(QLatin1String(KEY_TITLE), m_title);
    m_tr.setSource(QLatin1String(KEY_SUBTITLE), m_subtitle);
}

void CommonBlocHeader::setTitle(const QString &title)
{
    m_title = title;
    m_tr.setSource(QLatin1String(KEY_TITLE), title);
}

const QString &CommonBlocHeader::title() const
{
    return m_title;
}

void CommonBlocHeader::setSubtitle(const QString &subtitle)
{
    m_subtitle = subtitle;
    m_tr.setSource(QLatin1String(KEY_SUBTITLE), subtitle);
}

const QString &CommonBlocHeader::subtitle() const
{
    return m_subtitle;
}

QHash<QString, QString> CommonBlocHeader::sourceTexts() const
{
    QHash<QString, QString> result;
    if (!m_title.isEmpty()) {
        result.insert(QLatin1String(KEY_TITLE), m_title);
    }
    if (!m_subtitle.isEmpty()) {
        result.insert(QLatin1String(KEY_SUBTITLE), m_subtitle);
    }
    return result;
}

void CommonBlocHeader::setTranslation(const QString &fieldId,
                                      const QString &langCode,
                                      const QString &translatedText)
{
    m_tr.setTranslation(fieldId, langCode, translatedText);
}

QStringList CommonBlocHeader::missingTranslations(const QString &langCode,
                                                  const QString &sourceLangCode) const
{
    if (langCode.isEmpty() || langCode == sourceLangCode) {
        return {};
    }
    return m_tr.missingFields(langCode);
}

void CommonBlocHeader::saveTranslations(QSettings &settings)
{
    m_tr.saveToSettings(settings);
}

void CommonBlocHeader::loadTranslations(QSettings &settings)
{
    m_tr.loadFromSettings(settings);
}
