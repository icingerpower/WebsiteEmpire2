#include "PageBlocHubGrid.h"

#include "website/AbstractEngine.h"
#include "website/pages/IPageRepository.h"
#include "website/theme/AbstractTheme.h"
#include "website/pages/PageRecord.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/widgets/PageBlocHubGridWidget.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <algorithm>

// =============================================================================
// File-local helpers
// =============================================================================

namespace {

QString permalinkToTitle(const QString &permalink)
{
    QString title = permalink;
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
    return title;
}

// Extract the H1 title from article body text that uses [TITLE level="1"]...[/TITLE].
// Returns an empty string if not found; caller falls back to permalinkToTitle().
QString extractH1Title(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral("\\[TITLE\\b[^\\]]*level=[\"']1[\"'][^\\]]*\\]([^\\[]+)\\[/TITLE\\]"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(text);
    if (m.hasMatch()) {
        return m.captured(1).trimmed();
    }
    return {};
}

// Collects sentences until either maxSentences is reached OR totalChars >= targetChars,
// whichever comes first.  targetChars=0 disables the character threshold.
// This prevents very short excerpts in languages with brief introductory sentences.
QString extractExcerpt(const QString &rawText, qsizetype maxSentences, qsizetype targetChars = 0)
{
    if (rawText.isEmpty()) {
        return {};
    }
    QString plain;
    const auto &paras = rawText.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts);
    for (const QString &para : paras) {
        const QString &trimmed = para.trimmed();
        if (trimmed.startsWith(QLatin1Char('['))) {
            continue;
        }
        QString clean;
        clean.reserve(trimmed.size());
        int depth = 0;
        for (const QChar &ch : trimmed) {
            if (ch == QLatin1Char('[')) {
                ++depth;
            } else if (ch == QLatin1Char(']')) {
                if (depth > 0) {
                    --depth;
                }
            } else if (depth == 0) {
                clean += ch;
            }
        }
        const QString &cleanTrimmed = clean.trimmed();
        if (!cleanTrimmed.isEmpty()) {
            if (!plain.isEmpty()) {
                plain += QLatin1Char(' ');
            }
            plain += cleanTrimmed;
        }
    }
    if (plain.isEmpty()) {
        return {};
    }
    QStringList sentences;
    qsizetype totalChars = 0;
    qsizetype start = 0;
    const qsizetype len = plain.size();
    for (qsizetype i = 0; i < len; ++i) {
        const QChar c = plain.at(i);
        if (c != QLatin1Char('.') && c != QLatin1Char('!') && c != QLatin1Char('?')) {
            continue;
        }
        if (i + 1 < len && plain.at(i + 1) != QLatin1Char(' ')) {
            continue;
        }
        const QString sentence = plain.mid(start, i - start + 1).trimmed();
        sentences.append(sentence);
        totalChars += sentence.size();
        start = i + 2;
        if (sentences.size() >= maxSentences || (targetChars > 0 && totalChars >= targetChars)) {
            break;
        }
    }
    const bool underLimit = sentences.size() < maxSentences
                            && (targetChars == 0 || totalChars < targetChars);
    if (underLimit && start < len) {
        const QString &tail = plain.mid(start).trimmed();
        if (!tail.isEmpty()) {
            sentences.append(tail);
        }
    }
    return sentences.join(QStringLiteral(" "));
}

struct ArticleEntry {
    PageRecord record;
    QString    effectivePermalink; ///< language-specific permalink (may differ from record.permalink)
    QString    title;
    QString    excerpt;
};

} // namespace

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
    const QString &lang = engine.getLangCode(websiteIndex);
    // Article text lives at bloc index 1 ("1_text"); its inline translation
    // key follows the same prefix: "1_tr:<lang>:text".
    const QString textKey = QStringLiteral("1_tr:") + lang + QStringLiteral(":text");
    QList<ArticleEntry> entries;

    const auto &allPages = m_repo->findAll();
    for (const auto &record : std::as_const(allPages)) {
        if (record.typeId != QStringLiteral("article")
                || !engine.isPageAvailable(record.permalink, websiteIndex)) {
            continue;
        }
        const auto &data = m_repo->loadData(record.id);

        // Scan every *_categories key (breadcrumb bloc AND cross-reference bloc)
        // so that sub-category assignments in cross-references also populate hubs.
        bool inCategory = false;
        for (auto it = data.constBegin(); it != data.constEnd() && !inCategory; ++it) {
            if (!it.key().endsWith(QStringLiteral("_categories"))) {
                continue;
            }
            const auto &parts = it.value().split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const auto &part : std::as_const(parts)) {
                if (catIds.contains(part.trimmed().toInt())) {
                    inCategory = true;
                    break;
                }
            }
        }
        if (inCategory) {
            const QString &translatedText = data.value(textKey);
            if (translatedText.isEmpty() && lang != record.lang) {
                continue; // not yet translated for this language — skip
            }
            ArticleEntry entry;
            entry.record = record;

            // Compute the language-specific permalink (mirrors PageGenerator logic).
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
            entry.title = extractH1Title(bodyForTitle);
            if (entry.title.isEmpty()) {
                entry.title = permalinkToTitle(ep);
            }
            entry.excerpt = extractExcerpt(bodyForTitle, 4, 200);
            entries.append(entry);
        }
    }

    if (entries.isEmpty()) {
        return;
    }

    // ── Load stats and sort: CTR → views → title (alphabetical) ──────
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

    if (entries.size() > m_maxArticles) {
        entries = entries.mid(0, m_maxArticles);
    }

    // ── CSS (once per page) ────────────────────────────────────────────
    if (!cssDoneIds.contains(QStringLiteral("page-bloc-hub-grid"))) {
        cssDoneIds.insert(QStringLiteral("page-bloc-hub-grid"));

        const AbstractTheme *theme = engine.getActiveTheme();
        const QString primary = theme ? theme->primaryColor() : QStringLiteral("#1a73e8");

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
                "box-shadow:0 2px 12px rgba(0,0,0,.07);"
                "border-left:4px solid ");
        css += primary;
        css += QStringLiteral(
            ";"
                "padding:1.25rem;"
                "display:flex;"
                "flex-direction:column;"
                "gap:.5rem;"
                "transition:transform .2s,box-shadow .2s"
            "}"
            ".hub-card:hover{"
                "transform:translateY(-3px);"
                "box-shadow:0 6px 20px rgba(0,0,0,.12)"
            "}"
            ".hub-card__link{"
                "display:block;"
                "text-decoration:none;"
                "color:#1a1a1a"
            "}"
            ".hub-card__title{"
                "font-size:1.05rem;"
                "font-weight:600;"
                "margin:0;"
                "line-height:1.4;"
                "color:#1a1a1a;"
                "transition:color .15s"
            "}"
            ".hub-card__link:hover .hub-card__title{color:");
        css += primary;
        css += QStringLiteral(
            "}"
            ".hub-card__excerpt{"
                "font-size:.875rem;"
                "color:#1a1a1a;"
                "margin:0;"
                "line-height:1.6"
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

    for (const auto &entry : std::as_const(entries)) {
        html += QStringLiteral("<article class=\"hub-card\" data-hub-permalink=\"");
        html += entry.effectivePermalink;
        html += QStringLiteral("\">");
        html += QStringLiteral("<a href=\"");
        html += entry.effectivePermalink;
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

AbstractPageBlockWidget *PageBlocHubGrid::createEditWidget()
{
    return new PageBlocHubGridWidget(m_categoryTable);
}
