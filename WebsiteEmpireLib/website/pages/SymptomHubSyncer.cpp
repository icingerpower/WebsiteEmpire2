#include "SymptomHubSyncer.h"

#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageTypeSymptomHub.h"
#include "website/pages/PageTypeSymptomIndex.h"
#include "website/pages/blocs/PageBlocSymptomLinks.h"  // SymptomNav::slugify

#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSet>

// =============================================================================
// Constructor
// =============================================================================

SymptomHubSyncer::SymptomHubSyncer(IPageRepository &repo)
    : m_repo(repo)
{
}

// =============================================================================
// syncStubs
// =============================================================================

int SymptomHubSyncer::syncStubs(const QDir &workingDir, const QString &lang)
{
    const QString dbPath = workingDir.filePath(
        QStringLiteral("results_db/PageAttributesHealthSymptom.db"));
    if (!QFile::exists(dbPath)) {
        return 0;
    }

    // Build set of permalinks already in pages.db to avoid duplicates.
    QSet<QString> existingPermalinks;
    const QList<PageRecord> &all = m_repo.findAll();
    existingPermalinks.reserve(all.size());
    for (const PageRecord &r : std::as_const(all)) {
        existingPermalinks.insert(r.permalink);
    }

    int created = 0;

    // Create /symptoms index page if absent.
    const QString indexPermalink = QStringLiteral("/symptoms");
    if (!existingPermalinks.contains(indexPermalink)) {
        m_repo.create(QLatin1String(PageTypeSymptomIndex::TYPE_ID), indexPermalink, lang);
        existingPermalinks.insert(indexPermalink);
        ++created;
    }

    // Open symptom DB and create one hub stub per symptom.
    {
        static int s_seed = 0;
        const QString connName = QStringLiteral("symptom_syncer_") + QString::number(++s_seed);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(dbPath);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            if (db.open()) {
                QSqlQuery q(db);
                q.exec(QStringLiteral(
                    "SELECT health_symptom_name FROM records ORDER BY health_symptom_name COLLATE NOCASE"));
                while (q.next()) {
                    const QString name = q.value(0).toString().trimmed();
                    if (name.isEmpty()) {
                        continue;
                    }
                    const QString slug      = SymptomNav::slugify(name);
                    const QString permalink = QStringLiteral("/symptoms/") + slug;
                    if (existingPermalinks.contains(permalink)) {
                        continue;
                    }
                    m_repo.create(QLatin1String(PageTypeSymptomHub::TYPE_ID), permalink, lang);
                    existingPermalinks.insert(permalink);
                    ++created;
                }
            }
        }
        QSqlDatabase::removeDatabase(connName);
    }

    return created;
}
