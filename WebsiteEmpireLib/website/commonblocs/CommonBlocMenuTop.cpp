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

        QString primary   = QStringLiteral("#1a73e8");
        QString secondary = QStringLiteral("#fbbc04");
        QString bodyText  = QStringLiteral("#1f2937");
        if (theme) {
            primary   = theme->primaryColor();
            secondary = theme->secondaryColor();
            bodyText  = theme->bodyTextColor();
        }

        // Container — sticky, full-width primary bar
        css += QStringLiteral(".nav-top{display:flex;align-items:center;"
            "justify-content:space-between;padding:.5rem 1.5rem;"
            "position:sticky;top:0;z-index:100;background:");
        css += primary;
        css += QStringLiteral(";box-shadow:0 2px 8px rgba(0,0,0,.2)}");

        // Hamburger — hidden on desktop (display:none default), flex on mobile
        css += QStringLiteral(".nav-top__hamburger{display:none;flex-direction:column;"
            "justify-content:space-between;width:24px;height:18px;"
            "background:none;border:none;cursor:pointer;padding:0;flex-shrink:0}");
        css += QStringLiteral(".nav-top__hamburger span{display:block;width:100%;height:2px;"
            "background:#fff;border-radius:1px}");

        // Nav list — desktop-first: always visible, horizontal row, wraps if needed
        css += QStringLiteral(".nav-top__list{display:flex;flex-direction:row;flex-wrap:wrap;"
            "align-items:center;list-style:none;margin:0;padding:0;gap:.125rem}");
        css += QStringLiteral(".nav-top__item{position:relative}");

        // Links — inline-flex so each item only takes the width it needs
        css += QStringLiteral(".nav-top__link,.nav-top__dropdown-toggle{"
            "display:inline-flex;align-items:center;padding:.375rem .75rem;"
            "text-decoration:none;color:rgba(255,255,255,.92);background:none;border:none;"
            "cursor:pointer;font:inherit;font-size:.9375rem;"
            "white-space:nowrap;border-radius:4px;"
            "transition:color .15s,background .15s}");
        css += QStringLiteral(".nav-top__link:hover,.nav-top__link:focus,"
            ".nav-top__dropdown-toggle:hover,.nav-top__dropdown-toggle:focus{"
            "color:#fff;background:rgba(255,255,255,.15)}");

        // Current page — secondary colour
        css += QStringLiteral(".nav-top__item--current>.nav-top__link,"
            ".nav-top__item--current>.nav-top__dropdown-toggle{"
            "color:");
        css += secondary;
        css += QStringLiteral(";font-weight:600}");

        // Important modifier
        css += QStringLiteral(".nav-top__link--important,"
            ".nav-top__dropdown-toggle--important{color:");
        css += secondary;
        css += QStringLiteral(";font-weight:700}");

        css += QStringLiteral(".nav-top__dropdown-toggle{gap:.3rem}");

        // Desktop submenu — white card dropdown
        css += QStringLiteral(".nav-top__submenu{display:none;position:absolute;"
            "top:calc(100% + .375rem);left:0;"
            "list-style:none;margin:0;padding:.5rem 0;background:#fff;"
            "box-shadow:0 4px 20px rgba(0,0,0,.15);"
            "min-width:200px;border-radius:6px;z-index:1000}");
        css += QStringLiteral(".nav-top__submenu--open,"
            ".nav-top__item--has-children:hover>.nav-top__submenu,"
            ".nav-top__item--has-children:focus-within>.nav-top__submenu{display:block}");
        css += QStringLiteral(".nav-top__submenu-item{list-style:none}");
        css += QStringLiteral(".nav-top__submenu-link{display:block;padding:.5rem 1rem;"
            "text-decoration:none;font-size:.9375rem;white-space:nowrap;"
            "transition:color .15s,background .15s;color:");
        css += bodyText;
        css += QStringLiteral("}");
        css += QStringLiteral(".nav-top__submenu-link:hover,.nav-top__submenu-link:focus{"
            "background:rgba(0,0,0,.04);color:");
        css += primary;
        css += QStringLiteral("}");
        css += QStringLiteral(".nav-top__submenu-link--important{color:");
        css += primary;
        css += QStringLiteral(";font-weight:700}");

        // Mobile — hamburger on, list hidden behind toggle
        css += QStringLiteral("@media(max-width:899px){"
            ".nav-top__hamburger{display:flex}"
            ".nav-top__list{display:none;position:absolute;top:100%;left:0;right:0;"
                "flex-direction:column;flex-wrap:nowrap;padding:.5rem 0;background:");
        css += primary;
        css += QStringLiteral(";box-shadow:0 6px 16px rgba(0,0,0,.2);z-index:1000}"
            ".nav-top__list--open{display:flex}"
            ".nav-top__link,.nav-top__dropdown-toggle{width:100%;border-radius:0}"
            ".nav-top__submenu{position:static;display:none;"
                "box-shadow:none;background:none;"
                "padding:.25rem 0 .25rem 1.25rem;border-radius:0;min-width:auto}"
            ".nav-top__submenu--open{display:block}"
            ".nav-top__submenu-link{color:rgba(255,255,255,.8);font-size:.9375em}"
            ".nav-top__submenu-link:hover,.nav-top__submenu-link:focus{"
                "color:#fff;background:rgba(255,255,255,.12)}"
            ".nav-top__submenu-link--important{color:");
        css += secondary;
        css += QStringLiteral(";font-weight:700}"
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
            "var links=n.querySelectorAll('a[href]');"
            "var path=location.pathname;"
            "for(var j=0;j<links.length;j++){"
                "if(links[j].getAttribute('href')===path){"
                    "links[j].setAttribute('aria-current','page');"
                    "var li=links[j].closest('.nav-top__item,.nav-top__submenu-item');"
                    "if(li){li.classList.add('nav-top__item--current')}"
                "}"
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
            html += item.important
                ? QStringLiteral("<button class=\"nav-top__dropdown-toggle"
                                 " nav-top__dropdown-toggle--important\""
                                 " aria-expanded=\"false\" aria-haspopup=\"true\">")
                : QStringLiteral("<button class=\"nav-top__dropdown-toggle\""
                                 " aria-expanded=\"false\" aria-haspopup=\"true\">");
            html += resolveLabel(item.label, lang, sourceLang).toHtmlEscaped();
            // U+25BE BLACK DOWN-POINTING SMALL TRIANGLE
            html += QStringLiteral(" <span aria-hidden=\"true\">\u25be</span></button>");
            html += QStringLiteral("<ul class=\"nav-top__submenu\" role=\"menu\">");
            for (const auto &sub : std::as_const(item.children)) {
                html += QStringLiteral("<li class=\"nav-top__submenu-item\">");
                html += sub.important
                    ? QStringLiteral("<a class=\"nav-top__submenu-link"
                                     " nav-top__submenu-link--important\" href=\"")
                    : QStringLiteral("<a class=\"nav-top__submenu-link\" href=\"");
                html += engine.resolvePermalink(sub.url, websiteIndex).toHtmlEscaped();
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
            html += item.important
                ? QStringLiteral("<a class=\"nav-top__link nav-top__link--important\" href=\"")
                : QStringLiteral("<a class=\"nav-top__link\" href=\"");
            html += engine.resolvePermalink(item.url, websiteIndex).toHtmlEscaped();
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
