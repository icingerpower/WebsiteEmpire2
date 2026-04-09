#include "CommonBlocMenuBottom.h"

#include "website/AbstractEngine.h"
#include "website/theme/AbstractTheme.h"

#include <QCoreApplication>

QString CommonBlocMenuBottom::getId() const
{
    return QStringLiteral("menu_bottom");
}

QString CommonBlocMenuBottom::getName() const
{
    return QCoreApplication::translate("CommonBlocMenuBottom", "Bottom menu");
}

QList<AbstractCommonBloc::ScopeType> CommonBlocMenuBottom::supportedScopes() const
{
    // The bottom menu is global — not customised per theme or domain.
    return {ScopeType::Global};
}

void CommonBlocMenuBottom::addCode(QStringView     origContent,
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

    // Resolve translation context
    const QString lang = engine.getLangCode(websiteIndex);
    AbstractTheme *theme = engine.getActiveTheme();
    const QString sourceLang = theme ? theme->sourceLangCode() : QString();

    // -------------------------------------------------------------------------
    // CSS — emitted once per page
    // -------------------------------------------------------------------------
    if (!cssDoneIds.contains(QStringLiteral("nav_bottom"))) {
        cssDoneIds.insert(QStringLiteral("nav_bottom"));
        css += QStringLiteral(".nav-bottom{padding:.75rem 1rem}");
        css += QStringLiteral(".nav-bottom__list{display:flex;flex-wrap:wrap;"
            "gap:.25rem .75rem;list-style:none;margin:0;padding:0;justify-content:center}");
        css += QStringLiteral(".nav-bottom__link{text-decoration:none;"
            "color:inherit;font-size:.875em}");
        css += QStringLiteral(".nav-bottom__link:hover,.nav-bottom__link:focus{"
            "text-decoration:underline}");
    }

    // -------------------------------------------------------------------------
    // HTML
    // -------------------------------------------------------------------------
    html += QStringLiteral("<nav class=\"nav-bottom\" role=\"navigation\""
                           " aria-label=\"Footer navigation\">");
    html += QStringLiteral("<ul class=\"nav-bottom__list\">");
    for (const auto &item : std::as_const(m_items)) {
        html += QStringLiteral("<li><a class=\"nav-bottom__link\" href=\"");
        html += item.url.toHtmlEscaped();
        html += QStringLiteral("\"");
        const QString itemRel = buildRel(item.rel, item.newTab);
        if (!itemRel.isEmpty()) {
            html += QStringLiteral(" rel=\"");
            html += itemRel.toHtmlEscaped();
            html += QStringLiteral("\"");
        }
        if (item.newTab) {
            html += QStringLiteral(" target=\"_blank\"");
        }
        html += QStringLiteral(">");
        html += resolveLabel(item.label, lang, sourceLang).toHtmlEscaped();
        html += QStringLiteral("</a></li>");
    }
    html += QStringLiteral("</ul></nav>");
}
