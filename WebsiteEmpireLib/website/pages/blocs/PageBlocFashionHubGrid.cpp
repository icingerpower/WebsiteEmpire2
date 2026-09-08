#include "PageBlocFashionHubGrid.h"
#include "PageBlocArticleUtils.h"

#include "website/AbstractEngine.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/blocs/widgets/PageBlocFashionHubGridWidget.h"
#include "website/taxonomy/TaxonomyDb.h"
#include "website/theme/AbstractTheme.h"

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <algorithm>

namespace {

struct ArticleEntry {
    PageRecord record;
    QString    effectivePermalink;
    QString    title;
    QString    excerpt;
};

} // namespace

// =============================================================================
// bindContext
// =============================================================================

void PageBlocFashionHubGrid::bindContext(IPageRepository &repo, const QDir &workingDir)
{
    m_repo       = &repo;
    m_workingDir = workingDir;
}

// =============================================================================
// getName
// =============================================================================

QString PageBlocFashionHubGrid::getName() const
{
    return QCoreApplication::translate("PageBlocFashionHubGrid", "Fashion Hub Grid");
}

// =============================================================================
// load / save
// =============================================================================

void PageBlocFashionHubGrid::load(const QHash<QString, QString> &values)
{
    m_dimension = values.value(QLatin1String(KEY_DIMENSION));
    m_tagValue  = values.value(QLatin1String(KEY_TAG_VALUE));
}

void PageBlocFashionHubGrid::save(QHash<QString, QString> &values) const
{
    values.insert(QLatin1String(KEY_DIMENSION), m_dimension);
    values.insert(QLatin1String(KEY_TAG_VALUE),  m_tagValue);
}

// =============================================================================
// translatedTagName / lastRenderedCount
// =============================================================================

QString PageBlocFashionHubGrid::translatedTagName(const QString &langCode) const
{
    if (m_dimension.isEmpty() || m_tagValue.isEmpty()) {
        return {};
    }
    return TaxonomyDb(m_workingDir).translationFor(m_dimension, m_tagValue, langCode);
}

int PageBlocFashionHubGrid::lastRenderedCount() const
{
    return m_lastRenderedCount;
}

// =============================================================================
// _loadStats (private) — mirrors PageBlocHubGrid::_loadStats() exactly
// =============================================================================

QHash<QString, PageBlocFashionHubGrid::ArticleStats> PageBlocFashionHubGrid::_loadStats() const
{
    QHash<QString, ArticleStats> stats;

    if (!m_workingDir.exists()) {
        return stats;
    }

    const QString dbPath   = m_workingDir.filePath(QStringLiteral("stats.db"));
    const QString connName = QStringLiteral("fashion_hub_grid_stats_%1")
                                 .arg(static_cast<qulonglong>(
                                     reinterpret_cast<quintptr>(this)));

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        if (db.open()) {
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

void PageBlocFashionHubGrid::addCode(QStringView     /*origContent*/,
                                      AbstractEngine &engine,
                                      int             websiteIndex,
                                      QString        &html,
                                      QString        &css,
                                      QString        &js,
                                      QSet<QString>  &cssDoneIds,
                                      QSet<QString>  &jsDoneIds) const
{
    if (m_dimension.isEmpty() || m_tagValue.isEmpty() || !m_repo) {
        m_lastRenderedCount = 0;
        return;
    }

    const QString &lang = engine.getLangCode(websiteIndex);
    const QString textKey = QStringLiteral("1_tr:") + lang + QStringLiteral(":text");
    QList<ArticleEntry> entries;

    const auto &allPages = m_repo->findAll();
    for (const auto &record : std::as_const(allPages)) {
        if (record.typeId != QStringLiteral("article_fashion")
                || !engine.isPageAvailable(record.permalink, websiteIndex)) {
            continue;
        }
        const auto &data = m_repo->loadData(record.id);

        // PageBlocFashionTaxonomyLinks' storage key for m_dimension is
        // prefixed with its bloc index by AbstractPageType::save() (e.g.
        // "7_fashion_color") — match by suffix rather than an exact key,
        // the same tolerant pattern PageBlocHubGrid/CategoryHubSyncer already
        // use for "*_categories" so the index doesn't need to be hardcoded.
        bool tagged = false;
        const QString suffix = QLatin1Char('_') + m_dimension;
        for (auto it = data.constBegin(); it != data.constEnd() && !tagged; ++it) {
            if (!it.key().endsWith(suffix)) {
                continue;
            }
            const QStringList &tags = it.value().split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString &t : tags) {
                if (t.trimmed() == m_tagValue) {
                    tagged = true;
                    break;
                }
            }
        }
        if (!tagged) {
            continue;
        }

        const QString &translatedText = data.value(textKey);
        if (translatedText.isEmpty() && lang != record.lang) {
            continue; // not yet translated for this language — skip
        }
        ArticleEntry entry;
        entry.record = record;

        QString ep = record.permalink;
        if (!record.endPermalink.isEmpty() && lang != record.lang) {
            const QString trSlugKey = QStringLiteral("tr:") + lang
                                      + QStringLiteral(":_permalink_slug");
            const QString &trSlug = data.value(trSlugKey);
            if (!trSlug.isEmpty()) {
                ep = QLatin1Char('/') + trSlug;
            }
        }
        entry.effectivePermalink = ep;
        const QString &sourceText = data.value(QStringLiteral("1_text"));
        const QString &bodyForTitle = translatedText.isEmpty() ? sourceText : translatedText;
        entry.title = ArticleCardUtils::extractH1Title(bodyForTitle);
        if (entry.title.isEmpty()) {
            entry.title = ArticleCardUtils::permalinkToTitle(ep);
        }
        entry.excerpt = ArticleCardUtils::extractExcerpt(bodyForTitle, 4, 200);
        entries.append(entry);
    }

    if (entries.isEmpty()) {
        m_lastRenderedCount = 0;
        return;
    }

    const auto &statsMap = _loadStats();

    std::stable_sort(entries.begin(), entries.end(),
        [&statsMap](const ArticleEntry &a, const ArticleEntry &b) {
            const ArticleStats &sa = statsMap.value(a.record.permalink);
            const ArticleStats &sb = statsMap.value(b.record.permalink);
            if (sa.ctr != sb.ctr) {
                return sa.ctr > sb.ctr;
            }
            if (sa.views != sb.views) {
                return sa.views > sb.views;
            }
            return a.title < b.title;
        });

    if (entries.size() > MAX_ARTICLES) {
        entries = entries.mid(0, MAX_ARTICLES);
    }
    m_lastRenderedCount = entries.size();

    {
        const AbstractTheme *theme = engine.getActiveTheme();
        const QString primary = theme ? theme->primaryColor() : QStringLiteral("#1a73e8");
        ArticleCardUtils::addHubCardCss(css, cssDoneIds, primary);
    }

    if (!jsDoneIds.contains(QLatin1String(ArticleCardUtils::HUB_GRID_JS_ID))) {
        jsDoneIds.insert(QLatin1String(ArticleCardUtils::HUB_GRID_JS_ID));
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

    // Hand-rolled (not ArticleCardUtils::renderHubCard()) so the <article> tag
    // carries data-hub-permalink, which the click-tracking JS above requires —
    // renderHubCard() doesn't emit it. Mirrors PageBlocHubGrid::addCode() exactly.
    html += QStringLiteral("<div class=\"hub-grid\">");
    for (const auto &entry : std::as_const(entries)) {
        html += QStringLiteral("<article class=\"hub-card\" data-hub-permalink=\"");
        html += entry.effectivePermalink;
        html += QStringLiteral("\">");
        html += QStringLiteral("<a href=\"");
        html += entry.effectivePermalink.mid(entry.effectivePermalink.startsWith(QLatin1Char('/')) ? 1 : 0);
        html += QStringLiteral("\" class=\"hub-card__link\">");
        html += QStringLiteral("<h3 class=\"hub-card__title\">");
        html += entry.title;
        html += QStringLiteral("</h3>");
        html += QStringLiteral("</a>");
        if (!entry.excerpt.isEmpty()) {
            html += QStringLiteral("<p class=\"hub-card__excerpt\">");
            html += entry.excerpt.toHtmlEscaped();
            html += QStringLiteral("</p>");
        }
        html += QStringLiteral("</article>");
    }
    html += QStringLiteral("</div>");
}

// =============================================================================
// createEditWidget
// =============================================================================

AbstractPageBlockWidget *PageBlocFashionHubGrid::createEditWidget()
{
    return new PageBlocFashionHubGridWidget();
}
