#include "PageBlocHubGrid.h"

#include "website/AbstractEngine.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/widgets/PageBlocHubGridWidget.h"

#include <QCoreApplication>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <algorithm>

// =============================================================================
// Construction / bindContext
// =============================================================================

PageBlocHubGrid::PageBlocHubGrid(CategoryTable &categoryTable)
    : m_categoryTable(categoryTable)
{
}

void PageBlocHubGrid::bindContext(IPageRepository &repo, const QDir &workingDir)
{
    m_repo       = &repo;
    m_workingDir = workingDir;
}

// =============================================================================
// getName
// =============================================================================

QString PageBlocHubGrid::getName() const
{
    return QCoreApplication::translate("PageBlocHubGrid", "Hub Grid");
}

// =============================================================================
// load / save
// =============================================================================

void PageBlocHubGrid::load(const QHash<QString, QString> &values)
{
    m_selectedCategoryIds.clear();
    const QString &cats = values.value(QLatin1String(KEY_CATEGORIES));
    if (!cats.isEmpty()) {
        const auto &parts = cats.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const auto &part : std::as_const(parts)) {
            const int id = part.trimmed().toInt();
            if (id > 0) {
                m_selectedCategoryIds.append(id);
            }
        }
    }

    m_maxArticles = values.value(QLatin1String(KEY_MAX_ARTICLES),
                                  QStringLiteral("12")).toInt();
    if (m_maxArticles < 1) {
        m_maxArticles = 12;
    }
}

void PageBlocHubGrid::save(QHash<QString, QString> &values) const
{
    QStringList parts;
    parts.reserve(m_selectedCategoryIds.size());
    for (int id : std::as_const(m_selectedCategoryIds)) {
        parts.append(QString::number(id));
    }
    values.insert(QLatin1String(KEY_CATEGORIES),   parts.join(QLatin1Char(',')));
    values.insert(QLatin1String(KEY_MAX_ARTICLES), QString::number(m_maxArticles));
}

// =============================================================================
// _loadStats (private)
// =============================================================================

QHash<QString, PageBlocHubGrid::ArticleStats> PageBlocHubGrid::_loadStats() const
{
    QHash<QString, ArticleStats> stats;

    if (!m_workingDir.exists()) {
        return stats;
    }

    const QString dbPath   = m_workingDir.filePath(QStringLiteral("stats.db"));
    const QString connName = QStringLiteral("hub_grid_stats_%1")
                                 .arg(static_cast<qulonglong>(
                                     reinterpret_cast<quintptr>(this)));

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            // CTR from hub display/click tracking (page_id stored as "hub:<permalink>")
            QSqlQuery ctrQ(db);
            ctrQ.exec(QStringLiteral(
                "SELECT page_id, COUNT(*) AS d, COUNT(clicked_at) AS c "
                "FROM displays_clicks WHERE page_id LIKE 'hub:%' GROUP BY page_id"));
            while (ctrQ.next()) {
                QString pageId = ctrQ.value(0).toString();
                if (pageId.startsWith(QStringLiteral("hub:"))) {
                    pageId = pageId.mid(4);
                }
                const int displays = ctrQ.value(1).toInt();
                const int clicks   = ctrQ.value(2).toInt();
                if (displays > 0) {
                    stats[pageId].ctr = static_cast<double>(clicks) / displays;
                }
            }

            // View counts from organic page sessions
            QSqlQuery viewQ(db);
            viewQ.exec(QStringLiteral(
                "SELECT page_id, COUNT(*) AS v FROM page_session GROUP BY page_id"));
            while (viewQ.next()) {
                const QString &pageId = viewQ.value(0).toString();
                stats[pageId].views   = viewQ.value(1).toInt();
            }
        }
    }
    QSqlDatabase::removeDatabase(connName);

    return stats;
}

// =============================================================================
// addCode
// =============================================================================

void PageBlocHubGrid::addCode(QStringView     /*origContent*/,
                               AbstractEngine &engine,
                               int             websiteIndex,
                               QString        &html,
                               QString        &css,
                               QString        &js,
                               QSet<QString>  &cssDoneIds,
                               QSet<QString>  &jsDoneIds) const
{
    if (m_selectedCategoryIds.isEmpty() || !m_repo) {
        return;
    }

    // ── Collect articles belonging to any selected category ───────────
    const QSet<int> catIds(m_selectedCategoryIds.begin(), m_selectedCategoryIds.end());
    QList<PageRecord> articles;

    const auto &allPages = m_repo->findAll();
    for (const auto &record : std::as_const(allPages)) {
        if (record.typeId != QStringLiteral("article")
                || !engine.isPageAvailable(record.permalink, websiteIndex)) {
            continue;
        }
        const auto &data   = m_repo->loadData(record.id);
        const auto &catStr = data.value(QStringLiteral("0_categories"));
        if (catStr.isEmpty()) {
            continue;
        }
        const auto &parts = catStr.split(QLatin1Char(','), Qt::SkipEmptyParts);
        bool inCategory = false;
        for (const auto &part : std::as_const(parts)) {
            if (catIds.contains(part.trimmed().toInt())) {
                inCategory = true;
                break;
            }
        }
        if (inCategory) {
            articles.append(record);
        }
    }

    if (articles.isEmpty()) {
        return;
    }

    // ── Load stats and sort: CTR → views → recency ────────────────────
    const auto &statsMap = _loadStats();

    std::stable_sort(articles.begin(), articles.end(),
        [&statsMap](const PageRecord &a, const PageRecord &b) {
            const ArticleStats &sa = statsMap.value(a.permalink);
            const ArticleStats &sb = statsMap.value(b.permalink);
            if (sa.ctr != sb.ctr) {
                return sa.ctr > sb.ctr;
            }
            if (sa.views != sb.views) {
                return sa.views > sb.views;
            }
            return a.updatedAt > b.updatedAt;
        });

    if (articles.size() > m_maxArticles) {
        articles = articles.mid(0, m_maxArticles);
    }

    // ── CSS (once per page) ────────────────────────────────────────────
    if (!cssDoneIds.contains(QStringLiteral("page-bloc-hub-grid"))) {
        cssDoneIds.insert(QStringLiteral("page-bloc-hub-grid"));
        css += QStringLiteral(
            ".hub-grid{"
                "display:grid;"
                "grid-template-columns:repeat(auto-fill,minmax(280px,1fr));"
                "gap:1.5rem;"
                "padding:1rem 0"
            "}"
            ".hub-card{"
                "background:#fff;"
                "border-radius:.75rem;"
                "box-shadow:0 2px 8px rgba(0,0,0,.1);"
                "overflow:hidden;"
                "transition:transform .2s,box-shadow .2s"
            "}"
            ".hub-card:hover{"
                "transform:translateY(-4px);"
                "box-shadow:0 6px 20px rgba(0,0,0,.15)"
            "}"
            ".hub-card__link{"
                "display:block;"
                "padding:1.25rem;"
                "text-decoration:none;"
                "color:inherit"
            "}"
            ".hub-card__title{"
                "font-size:1.05rem;"
                "font-weight:600;"
                "margin:0;"
                "line-height:1.4;"
                "color:#1a1a1a"
            "}");
    }

    // ── JS (once per page) ─────────────────────────────────────────────
    if (!jsDoneIds.contains(QStringLiteral("page-bloc-hub-grid"))) {
        jsDoneIds.insert(QStringLiteral("page-bloc-hub-grid"));
        // IntersectionObserver fires a POST /stats/display when a card becomes
        // visible; the returned display id is stored in data-hub-display-id for
        // later click tracking via PATCH /stats/click/{id}.
        js += QStringLiteral(
            "(function(){"
            "if(window.IntersectionObserver){"
            "var obs=new IntersectionObserver(function(entries){"
            "entries.forEach(function(e){"
            "if(!e.isIntersecting)return;"
            "obs.unobserve(e.target);"
            "var pl=e.target.dataset.hubPermalink;"
            "if(!pl)return;"
            "fetch('/stats/display',{"
                "method:'POST',"
                "headers:{'Content-Type':'application/json'},"
                "body:JSON.stringify({page_id:'hub:'+pl}),"
                "keepalive:true"
            "})"
            ".then(function(r){return r.json();})"
            ".then(function(d){if(d&&d.id)e.target.dataset.hubDisplayId=d.id;})"
            ".catch(function(){});"
            "});"
            "},{threshold:0.5});"
            "document.querySelectorAll('.hub-card[data-hub-permalink]')"
            ".forEach(function(el){obs.observe(el);});"
            "}"
            "document.addEventListener('click',function(e){"
            "var c=e.target.closest&&e.target.closest('.hub-card');"
            "if(!c||!c.dataset.hubDisplayId)return;"
            "fetch('/stats/click/'+c.dataset.hubDisplayId,{"
                "method:'PATCH',"
                "keepalive:true"
            "}).catch(function(){});"
            "},true);"
            "})();");
    }

    // ── HTML ───────────────────────────────────────────────────────────
    html += QStringLiteral("<div class=\"hub-grid\">");

    for (const auto &record : std::as_const(articles)) {
        // Derive display title from permalink (e.g. /my-article.html → My Article)
        QString title = record.permalink;
        if (title.startsWith(QLatin1Char('/'))) {
            title = title.mid(1);
        }
        if (title.endsWith(QStringLiteral(".html"))) {
            title.chop(5);
        }
        title.replace(QLatin1Char('-'), QLatin1Char(' '));
        bool capitalizeNext = true;
        for (int i = 0; i < title.size(); ++i) {
            if (title.at(i) == QLatin1Char(' ')) {
                capitalizeNext = true;
            } else if (capitalizeNext) {
                title[i] = title.at(i).toUpper();
                capitalizeNext = false;
            }
        }

        html += QStringLiteral("<article class=\"hub-card\" data-hub-permalink=\"");
        html += record.permalink;
        html += QStringLiteral("\">");
        html += QStringLiteral("<a href=\"");
        html += record.permalink;
        html += QStringLiteral("\" class=\"hub-card__link\">");
        html += QStringLiteral("<h3 class=\"hub-card__title\">");
        html += title;
        html += QStringLiteral("</h3>");
        html += QStringLiteral("</a>");
        html += QStringLiteral("</article>");
    }

    html += QStringLiteral("</div>");
}

// =============================================================================
// createEditWidget
// =============================================================================

AbstractPageBlockWidget *PageBlocHubGrid::createEditWidget()
{
    return new PageBlocHubGridWidget(m_categoryTable);
}
