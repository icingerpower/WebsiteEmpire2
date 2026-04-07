#include "PageBlocImageLinks.h"
#include "website/pages/blocs/widgets/PageBlocImageLinksWidget.h"

#include <QCoreApplication>

// =============================================================================
// getName
// =============================================================================

QString PageBlocImageLinks::getName() const
{
    return QCoreApplication::translate("PageBlocImageLinks", "Image Links");
}

// =============================================================================
// load / save
// =============================================================================

void PageBlocImageLinks::load(const QHash<QString, QString> &values)
{
    m_colsDesktop = values.value(QLatin1String(KEY_COLS_DESKTOP), QStringLiteral("4")).toInt();
    m_rowsDesktop = values.value(QLatin1String(KEY_ROWS_DESKTOP), QStringLiteral("2")).toInt();
    m_colsTablet  = values.value(QLatin1String(KEY_COLS_TABLET),  QStringLiteral("2")).toInt();
    m_rowsTablet  = values.value(QLatin1String(KEY_ROWS_TABLET),  QStringLiteral("3")).toInt();
    m_colsMobile  = values.value(QLatin1String(KEY_COLS_MOBILE),  QStringLiteral("1")).toInt();
    m_rowsMobile  = values.value(QLatin1String(KEY_ROWS_MOBILE),  QStringLiteral("4")).toInt();

    const int count = values.value(QLatin1String(KEY_ITEM_COUNT), QStringLiteral("0")).toInt();
    m_items.clear();
    m_items.reserve(count);

    for (int i = 0; i < count; ++i) {
        const auto &prefix = QStringLiteral("item_") + QString::number(i) + QStringLiteral("_");
        Item item;
        item.imageUrl   = values.value(prefix + QStringLiteral("img"));
        item.linkType   = values.value(prefix + QStringLiteral("type"));
        item.linkTarget = values.value(prefix + QStringLiteral("target"));
        item.altText    = values.value(prefix + QStringLiteral("alt"));
        m_items.append(std::move(item));
    }
}

void PageBlocImageLinks::save(QHash<QString, QString> &values) const
{
    values.insert(QLatin1String(KEY_COLS_DESKTOP), QString::number(m_colsDesktop));
    values.insert(QLatin1String(KEY_ROWS_DESKTOP), QString::number(m_rowsDesktop));
    values.insert(QLatin1String(KEY_COLS_TABLET),  QString::number(m_colsTablet));
    values.insert(QLatin1String(KEY_ROWS_TABLET),  QString::number(m_rowsTablet));
    values.insert(QLatin1String(KEY_COLS_MOBILE),  QString::number(m_colsMobile));
    values.insert(QLatin1String(KEY_ROWS_MOBILE),  QString::number(m_rowsMobile));

    values.insert(QLatin1String(KEY_ITEM_COUNT), QString::number(m_items.size()));

    for (int i = 0; i < m_items.size(); ++i) {
        const auto &prefix = QStringLiteral("item_") + QString::number(i) + QStringLiteral("_");
        const auto &item = m_items.at(i);
        values.insert(prefix + QStringLiteral("img"),    item.imageUrl);
        values.insert(prefix + QStringLiteral("type"),   item.linkType);
        values.insert(prefix + QStringLiteral("target"), item.linkTarget);
        values.insert(prefix + QStringLiteral("alt"),    item.altText);
    }
}

// =============================================================================
// addCode
// =============================================================================

void PageBlocImageLinks::addCode(QStringView     /*origContent*/,
                                  AbstractEngine &/*engine*/,
                                  int             /*websiteIndex*/,
                                  QString        &html,
                                  QString        &css,
                                  QString        &js,
                                  QSet<QString>  &cssDoneIds,
                                  QSet<QString>  &jsDoneIds) const
{
    // Check if any item has a non-empty imageUrl.
    bool hasAnyVisible = false;
    for (const auto &item : std::as_const(m_items)) {
        if (!item.imageUrl.isEmpty()) {
            hasAnyVisible = true;
            break;
        }
    }
    if (!hasAnyVisible) {
        return;
    }

    // ── HTML ────────────────────────────────────────────────────────────
    html += QStringLiteral("<section class=\"image-links-grid\" style=\"--cols-d:");
    html += QString::number(m_colsDesktop);
    html += QStringLiteral(";--cols-t:");
    html += QString::number(m_colsTablet);
    html += QStringLiteral(";--cols-m:");
    html += QString::number(m_colsMobile);
    html += QStringLiteral("\">");

    int visibleIndex = 0;
    for (const auto &item : std::as_const(m_items)) {
        if (item.imageUrl.isEmpty()) {
            continue;
        }
        const auto &href = resolveHref(item.linkType, item.linkTarget);
        html += QStringLiteral("<a href=\"");
        html += href;
        html += QStringLiteral("\" class=\"image-link\" data-link-id=\"");
        html += QString::number(visibleIndex);
        html += QStringLiteral("\"><img src=\"");
        html += item.imageUrl;
        html += QStringLiteral("\" alt=\"");
        html += item.altText;
        html += QStringLiteral("\" loading=\"lazy\"></a>");
        ++visibleIndex;
    }

    html += QStringLiteral("</section>");

    // ── CSS (once per page) ─────────────────────────────────────────────
    if (!cssDoneIds.contains(QStringLiteral("page-bloc-image-links"))) {
        cssDoneIds.insert(QStringLiteral("page-bloc-image-links"));
        css += QStringLiteral(
            ".image-links-grid{display:grid;grid-template-columns:repeat(var(--cols-d,4),1fr);gap:1rem;margin:0;padding:0}"
            "@media(max-width:1023px){.image-links-grid{grid-template-columns:repeat(var(--cols-t,2),1fr)}}"
            "@media(max-width:639px){.image-links-grid{grid-template-columns:repeat(var(--cols-m,1),1fr)}}"
            ".image-link{display:block;overflow:hidden;text-decoration:none}"
            ".image-link img{width:100%;height:auto;display:block;object-fit:cover}");
    }

    // ── JS (once per page) ──────────────────────────────────────────────
    if (!jsDoneIds.contains(QStringLiteral("page-bloc-image-links"))) {
        jsDoneIds.insert(QStringLiteral("page-bloc-image-links"));
        js += QStringLiteral(
            "(function(){"
            "if(!window.IntersectionObserver)return;"
            "var _ilObs=new IntersectionObserver(function(entries){"
            "entries.forEach(function(e){"
            "if(!e.isIntersecting)return;"
            "_ilObs.unobserve(e.target);"
            "var id=e.target.dataset.linkId;"
            "if(navigator.sendBeacon)navigator.sendBeacon('/stats',new Blob([JSON.stringify({t:'il-display',id:id,p:location.pathname})],{type:'application/json'}));"
            "});"
            "},{threshold:0.5});"
            "document.querySelectorAll('.image-link[data-link-id]').forEach(function(el){_ilObs.observe(el);});"
            "document.addEventListener('click',function(e){"
            "var el=e.target.closest&&e.target.closest('.image-link[data-link-id]');"
            "if(!el)return;"
            "if(navigator.sendBeacon)navigator.sendBeacon('/stats',new Blob([JSON.stringify({t:'il-click',id:el.dataset.linkId,p:location.pathname})],{type:'application/json'}));"
            "},true);"
            "})();");
    }
}

// =============================================================================
// createEditWidget
// =============================================================================

AbstractPageBlockWidget *PageBlocImageLinks::createEditWidget()
{
    return new PageBlocImageLinksWidget;
}

// =============================================================================
// Accessors
// =============================================================================

const QList<PageBlocImageLinks::Item> &PageBlocImageLinks::items() const
{
    return m_items;
}

int PageBlocImageLinks::colsDesktop() const { return m_colsDesktop; }
int PageBlocImageLinks::rowsDesktop() const { return m_rowsDesktop; }
int PageBlocImageLinks::colsTablet() const  { return m_colsTablet; }
int PageBlocImageLinks::rowsTablet() const  { return m_rowsTablet; }
int PageBlocImageLinks::colsMobile() const  { return m_colsMobile; }
int PageBlocImageLinks::rowsMobile() const  { return m_rowsMobile; }

// =============================================================================
// resolveHref  (private static)
// =============================================================================

QString PageBlocImageLinks::resolveHref(const QString &linkType, const QString &linkTarget)
{
    if (linkType == QLatin1String(LINK_TYPE_CATEGORY)) {
        return QStringLiteral("/category/") + linkTarget;
    }
    if (linkType == QLatin1String(LINK_TYPE_PAGE)) {
        return QStringLiteral("/") + linkTarget;
    }
    // "url" or any unknown type: use target as-is.
    return linkTarget;
}
