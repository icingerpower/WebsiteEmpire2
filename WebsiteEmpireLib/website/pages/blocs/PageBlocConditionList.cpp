#include "PageBlocConditionList.h"
#include "PageBlocArticleUtils.h"

#include "website/AbstractEngine.h"
#include "website/theme/AbstractTheme.h"
#include "website/pages/blocs/PageBlocSymptomLinks.h"   // for SymptomNav::slugify
#include "website/pages/blocs/widgets/PageBlocReadOnlyWidget.h"
#include "website/taxonomy/TaxonomyDb.h"

#include <QCoreApplication>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>

// =============================================================================
// setRenderContext
// =============================================================================

void PageBlocConditionList::setRenderContext(const QString &permalink,
                                              const QDir    &workingDir) const
{
    m_permalink    = permalink;
    m_workingDir   = workingDir;
    m_contextBound = true;
}

// =============================================================================
// _resolveSymptomName
// =============================================================================

QString PageBlocConditionList::_resolveSymptomName() const
{
    // permalink: /symptoms/difficulty-swallowing → slug: difficulty-swallowing
    const QString prefix = QStringLiteral("/symptoms/");
    if (!m_permalink.startsWith(prefix)) {
        return {};
    }
    const QString slug    = m_permalink.mid(prefix.length());
    const QString dbPath  = m_workingDir.filePath(
        QStringLiteral("results_db/PageAttributesHealthSymptom.db"));
    if (!QFile::exists(dbPath)) {
        return {};
    }

    QString result;
    {
        static int s_seed = 0;
        const QString connName = QStringLiteral("condlist_sym_") + QString::number(++s_seed);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(dbPath);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            if (db.open()) {
                QSqlQuery q(db);
                q.exec(QStringLiteral("SELECT health_symptom_name FROM records"));
                while (q.next()) {
                    const QString name     = q.value(0).toString().trimmed();
                    const QString nameSlug = SymptomNav::slugify(name);
                    if (nameSlug == slug) {
                        result = name;
                        break;
                    }
                }
            }
        }
        QSqlDatabase::removeDatabase(connName);
    }
    return result;
}

// =============================================================================
// _loadConditionNames
// =============================================================================

QStringList PageBlocConditionList::_loadConditionNames(const QString &symptomName) const
{
    const QString dbPath = m_workingDir.filePath(
        QStringLiteral("results_db/PageAttributesHealthCondition.db"));
    if (!QFile::exists(dbPath) || symptomName.isEmpty()) {
        return {};
    }

    QStringList names;
    {
        static int s_seed = 0;
        const QString connName = QStringLiteral("condlist_cond_") + QString::number(++s_seed);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(dbPath);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            if (db.open()) {
                QSqlQuery q(db);
                q.exec(QStringLiteral(
                    "SELECT health_condition_name, health_condition_symptoms FROM records"));
                while (q.next()) {
                    const QString condName  = q.value(0).toString().trimmed();
                    const QString symptoms  = q.value(1).toString().trimmed();
                    const QStringList parts = symptoms.split(QLatin1Char(','), Qt::SkipEmptyParts);
                    for (const QString &part : std::as_const(parts)) {
                        if (part.trimmed().compare(symptomName, Qt::CaseInsensitive) == 0) {
                            names.append(condName);
                            break;
                        }
                    }
                }
            }
        }
        QSqlDatabase::removeDatabase(connName);
    }
    return names;
}

// =============================================================================
// _findArticlePermalink
// =============================================================================

QString PageBlocConditionList::_findArticlePermalink(const QString &conditionSlug) const
{
    const QString dbPath = m_workingDir.filePath(QStringLiteral("pages.db"));
    if (!QFile::exists(dbPath) || conditionSlug.isEmpty()) {
        return {};
    }

    QString permalink;
    {
        static int s_seed = 0;
        const QString connName = QStringLiteral("condlist_pg_") + QString::number(++s_seed);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(dbPath);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            if (db.open()) {
                QSqlQuery q(db);
                // Exact match first, then prefix match (handles endPermalink suffixes).
                q.prepare(QStringLiteral(
                    "SELECT permalink FROM pages"
                    " WHERE type_id = 'article'"
                    "   AND (permalink = :exact OR permalink LIKE :prefix ESCAPE '\\')"
                    " ORDER BY length(permalink) ASC LIMIT 1"));
                q.bindValue(QStringLiteral(":exact"),  QLatin1Char('/') + conditionSlug);
                q.bindValue(QStringLiteral(":prefix"),
                            QLatin1Char('/') + conditionSlug + QStringLiteral("-%"));
                if (q.exec() && q.next()) {
                    permalink = q.value(0).toString();
                }
            }
        }
        QSqlDatabase::removeDatabase(connName);
    }
    return permalink;
}

// =============================================================================
// _loadArticlesForSymptom
// =============================================================================

QStringList PageBlocConditionList::_loadArticlesForSymptomSlug(const QString &slug) const
{
    const QString dbPath = m_workingDir.filePath(QStringLiteral("pages.db"));
    if (!QFile::exists(dbPath) || slug.isEmpty()) {
        return {};
    }
    // Load every article's symptoms value and match each symptom name by slug in
    // C++.  This works even when the symptom has no entry in the aspire DB
    // (PageAttributesHealthSymptom.db), which is the common case for newly-assigned
    // symptoms.
    QStringList permalinks;
    {
        static int s_seed = 0;
        const QString connName = QStringLiteral("condlist_art_") + QString::number(++s_seed);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(dbPath);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            if (db.open()) {
                QSqlQuery q(db);
                q.exec(QStringLiteral(
                    "SELECT p.permalink, pd.value"
                    " FROM pages p"
                    " JOIN page_data pd ON pd.page_id = p.id"
                    " WHERE p.type_id = 'article'"
                    "   AND pd.key LIKE '%_symptoms'"
                    "   AND pd.value != ''"));
                while (q.next()) {
                    const QString plink    = q.value(0).toString();
                    const QString symptoms = q.value(1).toString();
                    for (const QString &part : symptoms.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                        if (SymptomNav::slugify(part.trimmed()) == slug) {
                            permalinks.append(plink);
                            break;
                        }
                    }
                }
            }
        }
        QSqlDatabase::removeDatabase(connName);
    }
    return permalinks;
}

// =============================================================================
// _loadArticleTexts
// =============================================================================

QHash<QString, QString> PageBlocConditionList::_loadArticleTexts(const QStringList &permalinks,
                                                                   const QString     &lang) const
{
    QHash<QString, QString> result;
    if (permalinks.isEmpty()) {
        return result;
    }
    const QString dbPath = m_workingDir.filePath(QStringLiteral("pages.db"));
    if (!QFile::exists(dbPath)) {
        return result;
    }

    QStringList placeholders;
    placeholders.reserve(permalinks.size());
    for (int i = 0; i < permalinks.size(); ++i) {
        placeholders.append(QStringLiteral("?"));
    }
    const QString inClause = placeholders.join(QLatin1Char(','));

    // Translated text is stored at key "1_tr:{lang}:text"; fall back to "1_text".
    const QString trKey = QStringLiteral("1_tr:") + lang + QStringLiteral(":text");

    {
        static int s_seed = 0;
        const QString connName = QStringLiteral("condlist_txt_") + QString::number(++s_seed);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(dbPath);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            if (db.open()) {
                QSqlQuery q(db);
                q.prepare(
                    QStringLiteral(
                        "SELECT p.permalink,"
                        " COALESCE(NULLIF(pd_tr.value,''),pd_en.value)"
                        " FROM pages p"
                        " LEFT JOIN page_data pd_en ON pd_en.page_id=p.id AND pd_en.key='1_text'"
                        " LEFT JOIN page_data pd_tr ON pd_tr.page_id=p.id AND pd_tr.key=?"
                        " WHERE p.permalink IN (") + inClause + QStringLiteral(")"));
                q.addBindValue(trKey);
                for (const QString &pl : std::as_const(permalinks)) {
                    q.addBindValue(pl);
                }
                if (q.exec()) {
                    while (q.next()) {
                        result.insert(q.value(0).toString(), q.value(1).toString());
                    }
                }
            }
        }
        QSqlDatabase::removeDatabase(connName);
    }
    return result;
}

// =============================================================================
// countConditions
// =============================================================================

int PageBlocConditionList::countConditions() const
{
    if (!m_contextBound) {
        return 0;
    }
    const QString prefix = QStringLiteral("/symptoms/");
    if (!m_permalink.startsWith(prefix)) {
        return 0;
    }
    const QString slug = m_permalink.mid(prefix.length());

    const QString symptomName = _resolveSymptomName();
    const int fromDb    = symptomName.isEmpty() ? 0 : _loadConditionNames(symptomName).size();
    const int fromPages = _loadArticlesForSymptomSlug(slug).size();

    if (fromDb == 0) {
        return fromPages;
    }
    if (fromPages == 0) {
        return fromDb;
    }
    return fromDb + fromPages;
}

// =============================================================================
// getName
// =============================================================================

QString PageBlocConditionList::getName() const
{
    return QCoreApplication::translate("PageBlocConditionList", "Condition List");
}

// =============================================================================
// load / save (no-ops)
// =============================================================================

void PageBlocConditionList::load(const QHash<QString, QString> &) {}
void PageBlocConditionList::save(QHash<QString, QString> &) const {}

// =============================================================================
// addCode
// =============================================================================

void PageBlocConditionList::addCode(QStringView,
                                     AbstractEngine &engine,
                                     int             websiteIndex,
                                     QString        &html,
                                     QString        &css,
                                     QString        &js,
                                     QSet<QString>  &cssDoneIds,
                                     QSet<QString>  &jsDoneIds) const
{
    Q_UNUSED(js)
    Q_UNUSED(jsDoneIds)

    if (!m_contextBound) {
        return;
    }

    const QString prefix = QStringLiteral("/symptoms/");
    if (!m_permalink.startsWith(prefix)) {
        return;
    }
    const QString slug = m_permalink.mid(prefix.length());

    // Build a merged list of {permalink, title, excerpt} from both sources.
    struct Entry { QString permalink; QString title; QString excerpt; };
    QList<Entry> entries;
    QSet<QString> seenPermalinks;

    // Phase 1: conditions DB path (condition name → find article by slug).
    const QString symptomName = _resolveSymptomName();
    if (!symptomName.isEmpty()) {
        const QStringList conditionNames = _loadConditionNames(symptomName);
        for (const QString &condName : std::as_const(conditionNames)) {
            const QString condSlug = SymptomNav::slugify(condName);
            const QString artPlink = _findArticlePermalink(condSlug);
            if (!artPlink.isEmpty()) {
                seenPermalinks.insert(artPlink);
            }
            // title will be enriched after batch text load; use condName as placeholder
            entries.append({artPlink, condName, {}});
        }
    }

    // Phase 2: direct page_data scan.
    const QStringList directLinks = _loadArticlesForSymptomSlug(slug);
    for (const QString &plink : std::as_const(directLinks)) {
        if (seenPermalinks.contains(plink)) {
            continue;
        }
        seenPermalinks.insert(plink);
        entries.append({plink, ArticleCardUtils::permalinkToTitle(plink), {}});
    }

    if (entries.isEmpty()) {
        return;
    }

    // Batch-load article body texts to extract real titles and excerpts.
    // Prefer translated text for the current language.
    const QString lang = engine.getLangCode(websiteIndex);
    QStringList allPermalinks;
    allPermalinks.reserve(entries.size());
    for (const Entry &e : std::as_const(entries)) {
        if (!e.permalink.isEmpty()) {
            allPermalinks.append(e.permalink);
        }
    }
    const QHash<QString, QString> texts = _loadArticleTexts(allPermalinks, lang);
    for (Entry &e : entries) {
        const QString &body = texts.value(e.permalink);
        if (!body.isEmpty()) {
            const QString h1 = ArticleCardUtils::extractH1Title(body);
            if (!h1.isEmpty()) {
                e.title = h1;
            }
            e.excerpt = ArticleCardUtils::extractExcerpt(body, 4, 200);
        }
        if (e.title.isEmpty()) {
            e.title = ArticleCardUtils::permalinkToTitle(e.permalink);
        }
    }

    // Symptom display name for <h1>: prefer TaxonomyDb translation, then
    // English canonical name, then URL-derived title.
    QString symptomDisplayName;
    if (!symptomName.isEmpty()) {
        // Found in aspire DB — look up translation directly by name.
        symptomDisplayName = TaxonomyDb(m_workingDir).translationFor(
            QStringLiteral("symptoms"), symptomName, lang);
        if (symptomDisplayName.isEmpty()) {
            symptomDisplayName = symptomName; // English fallback
        }
    } else {
        // Not in aspire DB — find by slug in TaxonomyDb (covers symptoms that
        // exist in pages but were never scraped into the aspire DB).
        const auto allSymptoms = TaxonomyDb(m_workingDir).loadTranslated(
            QStringLiteral("symptoms"), lang);
        for (const auto &[englishName, displayName] : std::as_const(allSymptoms)) {
            if (SymptomNav::slugify(englishName) == slug) {
                symptomDisplayName = displayName;
                break;
            }
        }
        if (symptomDisplayName.isEmpty()) {
            // Last resort: title-case just the slug (not the full path).
            symptomDisplayName = ArticleCardUtils::permalinkToTitle(
                QLatin1Char('/') + slug);
        }
    }

    // CSS — shared with PageBlocHubGrid (same CSS_ID, emitted once per page).
    {
        const AbstractTheme *theme = engine.getActiveTheme();
        const QString primary = theme ? theme->primaryColor() : QStringLiteral("#1a73e8");
        ArticleCardUtils::addHubCardCss(css, cssDoneIds, primary);
    }

    // Back-link CSS (only needed here, not in HubGrid).
    static const QString BACKLINK_CSS_ID = QStringLiteral("symptom-back-link");
    if (!cssDoneIds.contains(BACKLINK_CSS_ID)) {
        cssDoneIds.insert(BACKLINK_CSS_ID);
        css += QStringLiteral(
            ".symptom-back-link{display:inline-block;margin-top:1.5em;"
              "font-size:.9em;text-decoration:none}"
            ".symptom-back-link:hover{text-decoration:underline}");
    }

    if (!symptomDisplayName.isEmpty()) {
        html += QStringLiteral("<h1>");
        html += symptomDisplayName.toHtmlEscaped();
        html += QStringLiteral("</h1>");
    }

    html += QStringLiteral("<h2>");
    html += QCoreApplication::translate("PageBlocConditionList", "Possible conditions");
    html += QStringLiteral("</h2>");

    html += QStringLiteral("<div class=\"hub-grid\">");
    for (const Entry &e : std::as_const(entries)) {
        if (e.permalink.isEmpty() || !engine.isPageAvailable(e.permalink, websiteIndex)) {
            continue;
        }
        const QString resolved = engine.resolvePermalink(e.permalink, websiteIndex);
        ArticleCardUtils::renderHubCard(html, resolved, e.title, e.excerpt);
    }
    html += QStringLiteral("</div>");

    // Back-link to the symptom index.
    const QString indexPermalink = QStringLiteral("/symptoms");
    const QString indexResolved  = engine.resolvePermalink(indexPermalink, websiteIndex);
    if (!indexResolved.isEmpty() && engine.isPageAvailable(indexPermalink, websiteIndex)) {
        html += QStringLiteral("<a href=\"");
        html += indexResolved;
        html += QStringLiteral("\" class=\"symptom-back-link\">← ");
        html += QCoreApplication::translate("PageBlocConditionList", "All symptoms");
        html += QStringLiteral("</a>");
    }
}

// =============================================================================
// createEditWidget
// =============================================================================

AbstractPageBlockWidget *PageBlocConditionList::createEditWidget()
{
    return new PageBlocReadOnlyWidget(
        QCoreApplication::translate("PageBlocConditionList",
            "The condition list is generated from the aspire database at render time. "
            "No editing required."));
}
