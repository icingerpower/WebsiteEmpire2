#include "PageBlocConditionList.h"

#include "website/AbstractEngine.h"
#include "website/pages/blocs/PageBlocSymptomLinks.h"   // for SymptomNav::slugify
#include "website/pages/blocs/widgets/PageBlocReadOnlyWidget.h"

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
// countConditions
// =============================================================================

int PageBlocConditionList::countConditions() const
{
    if (!m_contextBound) {
        return 0;
    }
    const QString symptomName = _resolveSymptomName();
    if (symptomName.isEmpty()) {
        return 0;
    }
    return _loadConditionNames(symptomName).size();
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

    const QString symptomName = _resolveSymptomName();
    if (symptomName.isEmpty()) {
        return;
    }

    const QStringList conditionNames = _loadConditionNames(symptomName);
    if (conditionNames.isEmpty()) {
        return;
    }

    static const QString CSS_ID = QStringLiteral("condition-list-bloc");
    if (!cssDoneIds.contains(CSS_ID)) {
        cssDoneIds.insert(CSS_ID);
        css += QStringLiteral(
            ".condition-list{margin:1.5em 0;padding:0;list-style:none}"
            ".condition-list li{border-bottom:1px solid #e5e7eb;padding:.6em 0}"
            ".condition-list li:last-child{border-bottom:none}"
            ".condition-list a{text-decoration:none;font-weight:500}"
            ".condition-list a:hover{text-decoration:underline}"
            ".symptom-back-link{display:inline-block;margin-top:1.5em;"
            "font-size:.9em;color:#1a73e8;text-decoration:none}"
            ".symptom-back-link:hover{text-decoration:underline}");
    }

    html += QStringLiteral("<h2>");
    html += QCoreApplication::translate("PageBlocConditionList", "Possible conditions");
    html += QStringLiteral("</h2>");
    html += QStringLiteral("<ol class=\"condition-list\">");

    for (const QString &condName : std::as_const(conditionNames)) {
        const QString condSlug  = SymptomNav::slugify(condName);
        const QString artPlink  = _findArticlePermalink(condSlug);
        const QString resolved  = artPlink.isEmpty()
                                  ? QString{}
                                  : engine.resolvePermalink(artPlink, websiteIndex);

        html += QStringLiteral("<li>");
        if (!resolved.isEmpty() && engine.isPageAvailable(artPlink, websiteIndex)) {
            html += QStringLiteral("<a href=\"");
            html += resolved.startsWith(QLatin1Char('/')) ? resolved.mid(1) : resolved;
            html += QStringLiteral("\">");
            html += condName;
            html += QStringLiteral("</a>");
        } else {
            html += condName;
        }
        html += QStringLiteral("</li>");
    }

    html += QStringLiteral("</ol>");

    // Back-link to the symptom index.
    const QString indexPermalink = QStringLiteral("/symptoms");
    const QString indexResolved  = engine.resolvePermalink(indexPermalink, websiteIndex);
    if (!indexResolved.isEmpty() && engine.isPageAvailable(indexPermalink, websiteIndex)) {
        html += QStringLiteral("<a href=\"");
        html += indexResolved.startsWith(QLatin1Char('/')) ? indexResolved.mid(1) : indexResolved;
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
