#include "CommonBlocHeader.h"
#include "website/commonblocs/widgets/WidgetCommonBlocHeader.h"
#include "website/AbstractEngine.h"
#include "website/theme/AbstractTheme.h"

#include <QCoreApplication>
#include <QSet>

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
    Q_UNUSED(websiteIndex)
    Q_UNUSED(js)
    Q_UNUSED(jsDoneIds)

    if (m_title.isEmpty()) {
        return;
    }

    if (!cssDoneIds.contains(QStringLiteral("site_header"))) {
        cssDoneIds.insert(QStringLiteral("site_header"));

        QString primary   = QStringLiteral("#1a73e8");
        QString secondary = QStringLiteral("#fbbc04");
        QString font      = QStringLiteral("sans-serif");

        if (AbstractTheme *theme = engine.getActiveTheme()) {
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
    html += m_title.toHtmlEscaped();
    html += QStringLiteral("</h1>");
    if (!m_subtitle.isEmpty()) {
        html += QStringLiteral("<p class=\"site-subtitle\">");
        html += m_subtitle.toHtmlEscaped();
        html += QStringLiteral("</p>");
    }
    html += QStringLiteral("</header>");
}

AbstractCommonBlocWidget *CommonBlocHeader::createEditWidget()
{
    return new WidgetCommonBlocHeader();
}

void CommonBlocHeader::setTitle(const QString &title)
{
    m_title = title;
}

const QString &CommonBlocHeader::title() const
{
    return m_title;
}

void CommonBlocHeader::setSubtitle(const QString &subtitle)
{
    m_subtitle = subtitle;
}

const QString &CommonBlocHeader::subtitle() const
{
    return m_subtitle;
}
