#include "CommonBlocMenuTop.h"

#include "website/AbstractEngine.h"
#include "website/theme/AbstractTheme.h"

#include <QCoreApplication>

QString CommonBlocMenuTop::getId() const
{
    return QStringLiteral("menu_top");
}

QString CommonBlocMenuTop::getName() const
{
    return QCoreApplication::translate("CommonBlocMenuTop", "Top menu");
}

QList<AbstractCommonBloc::ScopeType> CommonBlocMenuTop::supportedScopes() const
{
    return {ScopeType::Global, ScopeType::PerTheme, ScopeType::PerDomain};
}

void CommonBlocMenuTop::addCode(QStringView     origContent,
                                AbstractEngine &engine,
                                int             websiteIndex,
                                QString        &html,
                                QString        &css,
                                QString        &js,
                                QSet<QString>  &cssDoneIds,
                                QSet<QString>  &jsDoneIds) const
{
    Q_UNUSED(origContent) // common blocs are not driven by source text

    // Resolve translation context
    const QString lang = engine.getLangCode(websiteIndex);
    AbstractTheme *theme = engine.getActiveTheme();
    const QString sourceLang = theme ? theme->sourceLangCode() : QString();

    // -------------------------------------------------------------------------
    // CSS — emitted once per page
    // -------------------------------------------------------------------------
    if (!cssDoneIds.contains(QStringLiteral("nav_top"))) {
        cssDoneIds.insert(QStringLiteral("nav_top"));
        css += QStringLiteral(".nav-top{display:flex;align-items:center;"
            "justify-content:space-between;padding:.5rem 1rem;position:relative}");
        css += QStringLiteral(".nav-top__hamburger{display:flex;flex-direction:column;"
            "justify-content:space-between;width:24px;height:18px;"
            "background:none;border:none;cursor:pointer;padding:0;flex-shrink:0}");
        css += QStringLiteral(".nav-top__hamburger span{display:block;width:100%;height:2px;"
            "background:currentColor;border-radius:1px}");
        css += QStringLiteral(".nav-top__list{display:none;position:absolute;top:100%;left:0;"
            "right:0;flex-direction:column;list-style:none;margin:0;padding:.5rem 0;"
            "background:#fff;box-shadow:0 4px 12px rgba(0,0,0,.15);z-index:1000}");
        css += QStringLiteral(".nav-top__list--open{display:flex}");
        css += QStringLiteral(".nav-top__item{position:relative}");
        css += QStringLiteral(".nav-top__link,.nav-top__dropdown-toggle{"
            "display:block;padding:.5rem 1rem;text-decoration:none;color:inherit;"
            "background:none;border:none;cursor:pointer;font:inherit;"
            "text-align:left;width:100%;white-space:nowrap}");
        css += QStringLiteral(".nav-top__link:hover,.nav-top__link:focus,"
            ".nav-top__dropdown-toggle:hover,.nav-top__dropdown-toggle:focus{"
            "background:rgba(0,0,0,.05)}");
        css += QStringLiteral(".nav-top__dropdown-toggle{display:flex;align-items:center;gap:.25rem}");
        css += QStringLiteral(".nav-top__submenu{display:none;list-style:none;margin:0;"
            "padding:.25rem 0 .25rem 1rem}");
        css += QStringLiteral(".nav-top__submenu--open{display:block}");
        css += QStringLiteral(".nav-top__submenu-link{display:block;padding:.375rem 1rem;"
            "text-decoration:none;color:inherit;font-size:.9375em}");
        css += QStringLiteral(".nav-top__submenu-link:hover,.nav-top__submenu-link:focus{"
            "text-decoration:underline}");
        css += QStringLiteral("@media(min-width:768px){"
            ".nav-top__hamburger{display:none}"
            ".nav-top__list{display:flex;flex-direction:row;position:static;"
                "box-shadow:none;background:none;padding:0;gap:.125rem}"
            ".nav-top__submenu{position:absolute;top:100%;left:0;padding:.5rem 0;"
                "background:#fff;box-shadow:0 4px 16px rgba(0,0,0,.15);"
                "min-width:180px;border-radius:4px;z-index:1000}"
            ".nav-top__item--has-children:hover>.nav-top__submenu,"
            ".nav-top__item--has-children:focus-within>.nav-top__submenu{display:block}"
            "}");
    }

    // -------------------------------------------------------------------------
    // JS — emitted once per page
    // -------------------------------------------------------------------------
    if (!jsDoneIds.contains(QStringLiteral("nav_top"))) {
        jsDoneIds.insert(QStringLiteral("nav_top"));
        js += QStringLiteral("(function(){"
            "var n=document.querySelector('.nav-top');"
            "if(!n)return;"
            "var h=n.querySelector('.nav-top__hamburger');"
            "var l=n.querySelector('.nav-top__list');"
            "if(h&&l){"
                "h.addEventListener('click',function(){"
                    "var o=l.classList.toggle('nav-top__list--open');"
                    "h.setAttribute('aria-expanded',String(o))"
                "});"
                "document.addEventListener('click',function(e){"
                    "if(!n.contains(e.target)){"
                        "l.classList.remove('nav-top__list--open');"
                        "h.setAttribute('aria-expanded','false')"
                    "}"
                "});"
                "document.addEventListener('keydown',function(e){"
                    "if(e.key==='Escape'){"
                        "l.classList.remove('nav-top__list--open');"
                        "h.setAttribute('aria-expanded','false');"
                        "h.focus()"
                    "}"
                "})"
            "}"
            "var t=n.querySelectorAll('.nav-top__dropdown-toggle');"
            "for(var i=0;i<t.length;i++){"
                "t[i].addEventListener('click',function(e){"
                    "var s=e.currentTarget.nextElementSibling;"
                    "if(s){"
                        "var o=s.classList.toggle('nav-top__submenu--open');"
                        "e.currentTarget.setAttribute('aria-expanded',String(o))"
                    "}"
                "})"
            "}"
            "})();");
    }

    // -------------------------------------------------------------------------
    // HTML
    // -------------------------------------------------------------------------
    html += QStringLiteral("<nav class=\"nav-top\" role=\"navigation\""
                           " aria-label=\"Main navigation\">");
    html += QStringLiteral("<button class=\"nav-top__hamburger\""
                           " aria-expanded=\"false\""
                           " aria-controls=\"nav-top-list\""
                           " aria-label=\"Toggle navigation\">"
                           "<span></span><span></span><span></span></button>");
    html += QStringLiteral("<ul class=\"nav-top__list\" id=\"nav-top-list\">");

    for (const auto &item : std::as_const(m_items)) {
        const bool hasChildren = !item.children.isEmpty();
        if (hasChildren) {
            html += QStringLiteral("<li class=\"nav-top__item nav-top__item--has-children\">");
            html += QStringLiteral("<button class=\"nav-top__dropdown-toggle\""
                                   " aria-expanded=\"false\" aria-haspopup=\"true\">");
            html += resolveLabel(item.label, lang, sourceLang).toHtmlEscaped();
            // U+25BE BLACK DOWN-POINTING SMALL TRIANGLE
            html += QStringLiteral(" <span aria-hidden=\"true\">\u25be</span></button>");
            html += QStringLiteral("<ul class=\"nav-top__submenu\" role=\"menu\">");
            for (const auto &sub : std::as_const(item.children)) {
                html += QStringLiteral("<li class=\"nav-top__submenu-item\">");
                html += QStringLiteral("<a class=\"nav-top__submenu-link\" href=\"");
                html += sub.url.toHtmlEscaped();
                html += QStringLiteral("\"");
                const QString subRel = buildRel(sub.rel, sub.newTab);
                if (!subRel.isEmpty()) {
                    html += QStringLiteral(" rel=\"");
                    html += subRel.toHtmlEscaped();
                    html += QStringLiteral("\"");
                }
                if (sub.newTab) {
                    html += QStringLiteral(" target=\"_blank\"");
                }
                html += QStringLiteral(" role=\"menuitem\">");
                html += resolveLabel(sub.label, lang, sourceLang).toHtmlEscaped();
                html += QStringLiteral("</a></li>");
            }
            html += QStringLiteral("</ul></li>");
        } else {
            html += QStringLiteral("<li class=\"nav-top__item\">");
            html += QStringLiteral("<a class=\"nav-top__link\" href=\"");
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
    }

    html += QStringLiteral("</ul></nav>");
}
