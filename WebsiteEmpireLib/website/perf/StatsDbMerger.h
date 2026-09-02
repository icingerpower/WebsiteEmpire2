#ifndef STATSDBMERGER_H
#define STATSDBMERGER_H

#include <QString>
#include <QStringList>

class QSqlDatabase;

/**
 * Merges several per-language stats.db files (each Drogon instance writes its
 * own stats.db inside deploy/<lang>/ on the server) into a single destination
 * stats.db that the local consumers read (PagesStatsWidget, StatsDbDataSource,
 * PageBlocHubGrid, CategoryHubSyncer).
 *
 * The destination CONTENT is rebuilt on every call (DELETE FROM both tables,
 * then re-insert from the sources).  The server-side databases are the source
 * of truth and only ever grow, so rebuilding keeps repeated downloads
 * idempotent — merging on top of the previous merge would duplicate every row.
 * The file itself is kept (not deleted and recreated) so connections already
 * open on it — e.g. the PagesStatsWidget mounted in the Page Stats tab —
 * still point at the live inode and see the new rows on their next refresh.
 * Sources are attached one at a time (SQLite caps attached databases at 10,
 * and there can be more languages than that).
 *
 * Row ids are NOT preserved: every per-language database starts its
 * AUTOINCREMENT at 1, so ids collide across languages.  Nothing references
 * those ids locally — page_id (the permalink) is the only cross-table key and
 * is copied verbatim.
 *
 * Every source file must exist and be a readable SQLite database; a missing or
 * unreadable source raises ExceptionWithTitleText (callers filter out
 * languages that were never deployed BEFORE calling).
 */
class StatsDbMerger
{
public:
    struct MergeResult {
        int displaysClicks = 0;
        int pageSessions   = 0;
    };

    /**
     * Rebuilds destDbPath from the given source databases.
     * Sources downloaded from servers that predate bot tracking have no
     * is_bot column; their rows are merged with is_bot = 0 (counted as
     * human — the User-Agent was never stored).
     * Raises ExceptionWithTitleText on any failure (destination not writable,
     * source missing/corrupt) — never returns a partial silent result.
     */
    static MergeResult merge(const QStringList &sourceDbPaths,
                             const QString     &destDbPath);

    /**
     * Adds the is_bot column to displays_clicks and page_session on the open
     * connection when missing (files created before bot tracking).  Shared by
     * merge() and PagesStatsWidget so both can rely on the column existing.
     * Raises ExceptionWithTitleText on SQL failure.
     */
    static void ensureIsBotColumns(QSqlDatabase &db);
};

#endif // STATSDBMERGER_H
