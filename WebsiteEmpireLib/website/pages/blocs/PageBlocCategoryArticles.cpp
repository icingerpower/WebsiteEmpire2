#include "PageBlocCategoryArticles.h"

#include "website/pages/IPageRepository.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/widgets/PageBlocCategoryArticlesWidget.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>

// =============================================================================
// Construction
// =============================================================================

PageBlocCategoryArticles::PageBlocCategoryArticles(CategoryTable &categoryTable,
                                                   IPageRepository &repo)
    : m_categoryTable(categoryTable)
    , m_repo(repo)
{
}

// =============================================================================
// getName
// =============================================================================

QString PageBlocCategoryArticles::getName() const
{
    return QCoreApplication::translate("PageBlocCategoryArticles",
                                      "Category Articles");
}

// =============================================================================
// load / save
// =============================================================================

void PageBlocCategoryArticles::load(const QHash<QString, QString> &values)
{
    const int count = values.value(QLatin1String(KEY_ENTRY_COUNT),
                                   QStringLiteral("0")).toInt();
    m_entries.clear();
    m_entries.reserve(count);

    for (int i = 0; i < count; ++i) {
        const auto &prefix = QStringLiteral("entry_") + QString::number(i) + QStringLiteral("_");
        CategoryEntry entry;
        entry.categoryId = values.value(prefix + QStringLiteral("cat_id"),
                                        QStringLiteral("0")).toInt();
        entry.title      = values.value(prefix + QStringLiteral("title"));
        entry.itemCount  = values.value(prefix + QStringLiteral("item_count"),
                                        QStringLiteral("5")).toInt();
        entry.sortOrder  = values.value(prefix + QStringLiteral("sort"),
                                        QLatin1String(SORT_RECENT));
        m_entries.append(std::move(entry));
    }
}

void PageBlocCategoryArticles::save(QHash<QString, QString> &values) const
{
    values.insert(QLatin1String(KEY_ENTRY_COUNT),
                  QString::number(m_entries.size()));

    for (int i = 0; i < m_entries.size(); ++i) {
        const auto &prefix = QStringLiteral("entry_") + QString::number(i) + QStringLiteral("_");
        const auto &entry = m_entries.at(i);
        values.insert(prefix + QStringLiteral("cat_id"),
                      QString::number(entry.categoryId));
        values.insert(prefix + QStringLiteral("title"),    entry.title);
        values.insert(prefix + QStringLiteral("item_count"),
                      QString::number(entry.itemCount));
        values.insert(prefix + QStringLiteral("sort"),     entry.sortOrder);
    }
}

// =============================================================================
// addCode
// =============================================================================

void PageBlocCategoryArticles::addCode(QStringView     /*origContent*/,
                                       AbstractEngine &/*engine*/,
                                       int             /*websiteIndex*/,
                                       QString        &html,
                                       QString        &css,
                                       QString        &js,
                                       QSet<QString>  &cssDoneIds,
                                       QSet<QString>  &jsDoneIds) const
{
    if (m_entries.isEmpty()) {
        return;
    }

    // ── Build articlesByCategory map (one pass over all pages) ──────────
    QHash<int, QList<PageRecord>> articlesByCategory;

    const auto &allPages = m_repo.findAll();
    for (const auto &record : allPages) {
        if (record.typeId != QStringLiteral("article") || record.sourcePageId != 0) {
            continue;
        }
        const auto &data = m_repo.loadData(record.id);
        const auto &catValue = data.value(QStringLiteral("0_categories"));
        if (catValue.isEmpty()) {
            continue;
        }
        const auto &catParts = catValue.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const auto &part : catParts) {
            const int catId = part.trimmed().toInt();
            if (catId > 0) {
                articlesByCategory[catId].append(record);
            }
        }
    }

    // ── Check if any entry produces visible articles ───────────────────
    bool hasAnyVisible = false;
    for (const auto &entry : std::as_const(m_entries)) {
        if (entry.categoryId > 0 && articlesByCategory.contains(entry.categoryId)) {
            hasAnyVisible = true;
            break;
        }
    }
    if (!hasAnyVisible) {
        return;
    }

    // ── CSS (once per page) ────────────────────────────────────────────
    if (!cssDoneIds.contains(QStringLiteral("page-bloc-category-articles"))) {
        cssDoneIds.insert(QStringLiteral("page-bloc-category-articles"));
        css += QStringLiteral(
            ".cat-art-bloc{display:flex;flex-wrap:wrap;gap:1.5rem}"
            ".cat-art-section{flex:1;min-width:200px}"
            ".cat-art__title{font-size:1.25rem;margin:0 0 .75rem}"
            ".cat-art__list{list-style:none;padding:0;margin:0}"
            ".cat-art__item{padding:.3rem 0}"
            ".cat-art__link{text-decoration:none;color:inherit}"
            ".cat-art__link:hover{text-decoration:underline}");
    }

    // ── JS (once per page) ─────────────────────────────────────────────
    if (!jsDoneIds.contains(QStringLiteral("page-bloc-category-articles"))) {
        jsDoneIds.insert(QStringLiteral("page-bloc-category-articles"));
        js += QStringLiteral(
            "(function(){"
            "if(!window.IntersectionObserver)return;"
            "var _caObs=new IntersectionObserver(function(entries){"
            "entries.forEach(function(e){"
            "if(!e.isIntersecting)return;"
            "_caObs.unobserve(e.target);"
            "var id=e.target.dataset.sectionId;"
            "if(navigator.sendBeacon)navigator.sendBeacon('/stats',new Blob([JSON.stringify({t:'ca-section-display',id:id,p:location.pathname})],{type:'application/json'}));"
            "});"
            "},{threshold:0.5});"
            "document.querySelectorAll('.cat-art-section[data-section-id]').forEach(function(el){_caObs.observe(el);});"
            "document.addEventListener('click',function(e){"
            "var el=e.target.closest&&e.target.closest('.cat-art__link[data-link-id]');"
            "if(!el)return;"
            "if(navigator.sendBeacon)navigator.sendBeacon('/stats',new Blob([JSON.stringify({t:'ca-link-click',id:el.dataset.linkId,p:location.pathname})],{type:'application/json'}));"
            "},true);"
            "})();");
    }

    // ── HTML ───────────────────────────────────────────────────────────
    html += QStringLiteral("<div class=\"cat-art-bloc\">");

    for (int entryIdx = 0; entryIdx < m_entries.size(); ++entryIdx) {
        const auto &entry = m_entries.at(entryIdx);
        if (entry.categoryId <= 0) {
            continue;
        }
        if (!articlesByCategory.contains(entry.categoryId)) {
            continue;
        }

        // Get articles for this category.
        auto articles = articlesByCategory.value(entry.categoryId);
        if (articles.isEmpty()) {
            continue;
        }

        // Sort according to entry's sort order and limit to itemCount.
        QList<PageRecord> sortedArticles;

        if (entry.sortOrder == QLatin1String(SORT_RECENT)) {
            std::sort(articles.begin(), articles.end(),
                      [](const PageRecord &a, const PageRecord &b) {
                          return a.updatedAt > b.updatedAt;
                      });
            sortedArticles = articles.mid(0, entry.itemCount);

        } else if (entry.sortOrder == QLatin1String(SORT_PERFORMANCE)) {
            std::sort(articles.begin(), articles.end(),
                      [](const PageRecord &a, const PageRecord &b) {
                          return a.createdAt < b.createdAt;
                      });
            sortedArticles = articles.mid(0, entry.itemCount);

        } else if (entry.sortOrder == QLatin1String(SORT_COMBINED)) {
            // Build two sorted lists.
            auto recentList = articles;
            std::sort(recentList.begin(), recentList.end(),
                      [](const PageRecord &a, const PageRecord &b) {
                          return a.updatedAt > b.updatedAt;
                      });
            auto perfList = articles;
            std::sort(perfList.begin(), perfList.end(),
                      [](const PageRecord &a, const PageRecord &b) {
                          return a.createdAt < b.createdAt;
                      });

            // Interleave, deduplicate.
            QSet<int> seenIds;
            int idxR = 0;
            int idxP = 0;
            while (sortedArticles.size() < entry.itemCount
                   && (idxR < recentList.size() || idxP < perfList.size())) {
                if (idxR < recentList.size()) {
                    if (!seenIds.contains(recentList.at(idxR).id)) {
                        seenIds.insert(recentList.at(idxR).id);
                        sortedArticles.append(recentList.at(idxR));
                    }
                    ++idxR;
                }
                if (sortedArticles.size() >= entry.itemCount) {
                    break;
                }
                if (idxP < perfList.size()) {
                    if (!seenIds.contains(perfList.at(idxP).id)) {
                        seenIds.insert(perfList.at(idxP).id);
                        sortedArticles.append(perfList.at(idxP));
                    }
                    ++idxP;
                }
            }

        } else {
            // Unknown sort order: fall back to recent.
            std::sort(articles.begin(), articles.end(),
                      [](const PageRecord &a, const PageRecord &b) {
                          return a.updatedAt > b.updatedAt;
                      });
            sortedArticles = articles.mid(0, entry.itemCount);
        }

        if (sortedArticles.isEmpty()) {
            continue;
        }

        // Determine the display title: use entry.title if non-empty, otherwise
        // fall back to the category name from the table.
        const auto &displayTitle = entry.title.isEmpty()
            ? (m_categoryTable.categoryById(entry.categoryId)
                   ? m_categoryTable.categoryById(entry.categoryId)->name
                   : QString())
            : entry.title;

        html += QStringLiteral("<section class=\"cat-art-section\" data-section-id=\"");
        html += QString::number(entryIdx);
        html += QStringLiteral("\">");
        html += QStringLiteral("<h2 class=\"cat-art__title\">");
        html += displayTitle;
        html += QStringLiteral("</h2>");
        html += QStringLiteral("<ul class=\"cat-art__list\">");

        for (int artIdx = 0; artIdx < sortedArticles.size(); ++artIdx) {
            const auto &article = sortedArticles.at(artIdx);
            const auto &title = permalinkToTitle(article.permalink);

            html += QStringLiteral("<li class=\"cat-art__item\">");
            html += QStringLiteral("<a href=\"");
            html += article.permalink;
            html += QStringLiteral("\" class=\"cat-art__link\" data-link-id=\"");
            html += QString::number(entryIdx);
            html += QStringLiteral("-");
            html += QString::number(artIdx);
            html += QStringLiteral("\">");
            html += title;
            html += QStringLiteral("</a>");
            html += QStringLiteral("</li>");
        }

        html += QStringLiteral("</ul>");
        html += QStringLiteral("</section>");
    }

    html += QStringLiteral("</div>");
}

// =============================================================================
// createEditWidget
// =============================================================================

AbstractPageBlockWidget *PageBlocCategoryArticles::createEditWidget()
{
    return new PageBlocCategoryArticlesWidget(m_categoryTable);
}

// =============================================================================
// Accessors
// =============================================================================

const QList<PageBlocCategoryArticles::CategoryEntry> &PageBlocCategoryArticles::entries() const
{
    return m_entries;
}

// =============================================================================
// permalinkToTitle  (private static)
// =============================================================================

QString PageBlocCategoryArticles::permalinkToTitle(const QString &permalink)
{
    QString result = permalink;

    // Strip leading /
    if (result.startsWith(QLatin1Char('/'))) {
        result = result.mid(1);
    }

    // Strip trailing .html
    if (result.endsWith(QStringLiteral(".html"))) {
        result.chop(5);
    }

    // Replace - with space
    result.replace(QLatin1Char('-'), QLatin1Char(' '));

    // Capitalize first letter of each word
    bool capitalizeNext = true;
    for (int i = 0; i < result.size(); ++i) {
        if (result.at(i) == QLatin1Char(' ')) {
            capitalizeNext = true;
        } else if (capitalizeNext) {
            result[i] = result.at(i).toUpper();
            capitalizeNext = false;
        }
    }

    return result;
}
