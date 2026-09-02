#include "StatsDbMerger.h"

#include "ExceptionWithTitleText.h"

#include <QFile>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <atomic>

static std::atomic<int> s_mergerConnCounter{0};

namespace {

void execOrThrow(QSqlQuery &query, const QString &sql)
{
    if (!query.exec(sql)) {
        const QSqlError &error = query.lastError();
        ExceptionWithTitleText ex(QObject::tr("Stats Merge Failed"),
                                  QObject::tr("SQL error while merging stats databases:\n%1\n\nQuery:\n%2")
                                      .arg(error.text(), sql));
        ex.raise();
    }
}

// Returns true when the given table of the ATTACHed "src" schema has an
// is_bot column — servers not yet running the bot-tracking binary produce
// stats.db files without it.
bool srcHasIsBot(QSqlQuery &query, const QString &table)
{
    execOrThrow(query, QStringLiteral("PRAGMA src.table_info(") + table
                       + QStringLiteral(")"));
    while (query.next()) {
        if (query.value(1).toString() == QStringLiteral("is_bot")) {
            return true;
        }
    }
    return false;
}

StatsDbMerger::MergeResult mergeInto(QSqlDatabase &db, const QStringList &sourceDbPaths)
{
    StatsDbMerger::MergeResult result;
    QSqlQuery query(db);

    // Same schema as StatsDb::createSchema() (StaticWebsiteServeLib) — the
    // merged file must be readable by every existing stats.db consumer.
    execOrThrow(query, QStringLiteral(
        "CREATE TABLE IF NOT EXISTS displays_clicks ("
        "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  page_id    TEXT    NOT NULL,"
        "  display_at TEXT    NOT NULL,"
        "  clicked_at TEXT,"
        "  is_bot     INTEGER NOT NULL DEFAULT 0 CHECK(is_bot IN (0,1))"
        ")"));
    execOrThrow(query, QStringLiteral(
        "CREATE TABLE IF NOT EXISTS page_session ("
        "  id                   INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  page_id              TEXT    NOT NULL,"
        "  scrolling_percentage INTEGER NOT NULL CHECK(scrolling_percentage BETWEEN 0 AND 100),"
        "  time_on_page         INTEGER NOT NULL,"
        "  is_final_page        INTEGER NOT NULL CHECK(is_final_page IN (0,1)),"
        "  is_bot               INTEGER NOT NULL DEFAULT 0 CHECK(is_bot IN (0,1))"
        ")"));
    StatsDbMerger::ensureIsBotColumns(db);

    // Rebuild content in place: the server DBs only ever grow, so wiping and
    // re-inserting keeps repeated downloads idempotent without deleting the
    // file (which would strand already-open reader connections on a dead inode).
    execOrThrow(query, QStringLiteral("DELETE FROM displays_clicks"));
    execOrThrow(query, QStringLiteral("DELETE FROM page_session"));

    for (const QString &sourcePath : sourceDbPaths) {
        query.prepare(QStringLiteral("ATTACH DATABASE :path AS src"));
        query.bindValue(QStringLiteral(":path"), sourcePath);
        if (!query.exec()) {
            const QSqlError &error = query.lastError();
            ExceptionWithTitleText ex(QObject::tr("Stats Merge Failed"),
                                      QObject::tr("Cannot attach source database %1:\n%2")
                                          .arg(sourcePath, error.text()));
            ex.raise();
        }

        const QString displayBotExpr = srcHasIsBot(query, QStringLiteral("displays_clicks"))
                                           ? QStringLiteral("is_bot")
                                           : QStringLiteral("0");
        execOrThrow(query, QStringLiteral(
            "INSERT INTO main.displays_clicks (page_id, display_at, clicked_at, is_bot) "
            "SELECT page_id, display_at, clicked_at, ") + displayBotExpr
            + QStringLiteral(" FROM src.displays_clicks"));
        result.displaysClicks += query.numRowsAffected();

        const QString sessionBotExpr = srcHasIsBot(query, QStringLiteral("page_session"))
                                           ? QStringLiteral("is_bot")
                                           : QStringLiteral("0");
        execOrThrow(query, QStringLiteral(
            "INSERT INTO main.page_session "
            "  (page_id, scrolling_percentage, time_on_page, is_final_page, is_bot) "
            "SELECT page_id, scrolling_percentage, time_on_page, is_final_page, ")
            + sessionBotExpr + QStringLiteral(" FROM src.page_session"));
        result.pageSessions += query.numRowsAffected();

        execOrThrow(query, QStringLiteral("DETACH DATABASE src"));
    }

    return result;
}

} // namespace

StatsDbMerger::MergeResult StatsDbMerger::merge(const QStringList &sourceDbPaths,
                                                const QString     &destDbPath)
{
    for (const QString &sourcePath : sourceDbPaths) {
        // ATTACH silently CREATES a missing file — existence must be checked
        // up front or a typo'd path would merge an empty database.
        if (!QFile::exists(sourcePath)) {
            ExceptionWithTitleText ex(QObject::tr("Stats Merge Failed"),
                                      QObject::tr("Source database does not exist: %1").arg(sourcePath));
            ex.raise();
        }
    }

    MergeResult result;
    const QString connName = QStringLiteral("stats_merger_")
                             + QString::number(s_mergerConnCounter.fetch_add(1));
    try {
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(destDbPath);
            if (!db.open()) {
                ExceptionWithTitleText ex(QObject::tr("Stats Merge Failed"),
                                          QObject::tr("Cannot create %1:\n%2")
                                              .arg(destDbPath, db.lastError().text()));
                ex.raise();
            }
            result = mergeInto(db, sourceDbPaths);
        } // db and query destroyed here — SQLite lock released
    } catch (...) {
        QSqlDatabase::removeDatabase(connName);
        throw;
    }
    QSqlDatabase::removeDatabase(connName);

    return result;
}

void StatsDbMerger::ensureIsBotColumns(QSqlDatabase &db)
{
    QSqlQuery query(db);
    const QStringList tables = {QStringLiteral("displays_clicks"),
                                QStringLiteral("page_session")};
    for (const QString &table : tables) {
        execOrThrow(query, QStringLiteral("PRAGMA table_info(") + table
                           + QStringLiteral(")"));
        bool found = false;
        while (query.next()) {
            if (query.value(1).toString() == QStringLiteral("is_bot")) {
                found = true;
                break;
            }
        }
        if (!found) {
            execOrThrow(query, QStringLiteral("ALTER TABLE ") + table
                + QStringLiteral(" ADD COLUMN is_bot INTEGER NOT NULL DEFAULT 0"
                                 " CHECK(is_bot IN (0,1))"));
        }
    }
}
