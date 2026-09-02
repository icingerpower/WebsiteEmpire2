#include <QtTest>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVariant>

#include "ExceptionWithTitleText.h"
#include "website/perf/StatsDbMerger.h"

class Test_Website_StatsDbMerger : public QObject
{
    Q_OBJECT

private slots:
    void test_merger_combines_rows_from_multiple_sources();
    void test_merger_rerun_is_idempotent();
    void test_merger_preserves_row_values();
    void test_merger_keeps_open_connections_valid();
    void test_merger_missing_source_raises();
    void test_merger_empty_sources_wipe_destination();
    void test_merger_preserves_is_bot_flag();
    void test_merger_legacy_source_without_is_bot_defaults_to_zero();

private:
    // Creates a stats.db at path with the given number of display and session
    // rows, all tagged with pagePrefix so tests can tell sources apart.
    void createSourceDb(const QString &path, const QString &pagePrefix,
                        int displayRows, int sessionRows);

    int countRows(const QString &dbPath, const QString &table);
};

void Test_Website_StatsDbMerger::createSourceDb(const QString &path,
                                                const QString &pagePrefix,
                                                int displayRows, int sessionRows)
{
    const QString connName = QStringLiteral("test_source_") + path;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE displays_clicks ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  page_id TEXT NOT NULL,"
            "  display_at TEXT NOT NULL,"
            "  clicked_at TEXT)")));
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE page_session ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  page_id TEXT NOT NULL,"
            "  scrolling_percentage INTEGER NOT NULL,"
            "  time_on_page INTEGER NOT NULL,"
            "  is_final_page INTEGER NOT NULL)")));
        for (int i = 0; i < displayRows; ++i) {
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO displays_clicks (page_id, display_at, clicked_at) VALUES "
                "('%1/page-%2', '2026-08-30T10:00:00', %3)")
                .arg(pagePrefix, QString::number(i),
                     i % 2 == 0 ? QStringLiteral("'2026-08-30T10:01:00'")
                                : QStringLiteral("NULL"))));
        }
        for (int i = 0; i < sessionRows; ++i) {
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO page_session "
                "(page_id, scrolling_percentage, time_on_page, is_final_page) "
                "VALUES ('%1/page-%2', 50, 30, 1)")
                .arg(pagePrefix, QString::number(i))));
        }
    }
    QSqlDatabase::removeDatabase(connName);
}

int Test_Website_StatsDbMerger::countRows(const QString &dbPath, const QString &table)
{
    int count = -1;
    const QString connName = QStringLiteral("test_count_") + dbPath + table;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("SELECT COUNT(*) FROM ") + table) && q.next()) {
                count = q.value(0).toInt();
            }
        }
    }
    QSqlDatabase::removeDatabase(connName);
    return count;
}

void Test_Website_StatsDbMerger::test_merger_combines_rows_from_multiple_sources()
{
    QTemporaryDir dir;
    const QString srcEn = dir.filePath(QStringLiteral("stats-en.db"));
    const QString srcFr = dir.filePath(QStringLiteral("stats-fr.db"));
    const QString dest  = dir.filePath(QStringLiteral("stats.db"));
    createSourceDb(srcEn, QStringLiteral("/en"), 3, 2);
    createSourceDb(srcFr, QStringLiteral("/fr"), 5, 4);

    const StatsDbMerger::MergeResult result =
        StatsDbMerger::merge({srcEn, srcFr}, dest);

    QCOMPARE(result.displaysClicks, 8);
    QCOMPARE(result.pageSessions, 6);
    QCOMPARE(countRows(dest, QStringLiteral("displays_clicks")), 8);
    QCOMPARE(countRows(dest, QStringLiteral("page_session")), 6);
}

void Test_Website_StatsDbMerger::test_merger_rerun_is_idempotent()
{
    QTemporaryDir dir;
    const QString src  = dir.filePath(QStringLiteral("stats-en.db"));
    const QString dest = dir.filePath(QStringLiteral("stats.db"));
    createSourceDb(src, QStringLiteral("/en"), 4, 3);

    StatsDbMerger::merge({src}, dest);
    const StatsDbMerger::MergeResult second = StatsDbMerger::merge({src}, dest);

    // A second download of the same server data must not duplicate rows.
    QCOMPARE(second.displaysClicks, 4);
    QCOMPARE(countRows(dest, QStringLiteral("displays_clicks")), 4);
    QCOMPARE(countRows(dest, QStringLiteral("page_session")), 3);
}

void Test_Website_StatsDbMerger::test_merger_preserves_row_values()
{
    QTemporaryDir dir;
    const QString src  = dir.filePath(QStringLiteral("stats-en.db"));
    const QString dest = dir.filePath(QStringLiteral("stats.db"));
    createSourceDb(src, QStringLiteral("/en"), 2, 1);

    StatsDbMerger::merge({src}, dest);

    const QString connName = QStringLiteral("test_values");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dest);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral(
            "SELECT page_id, display_at, clicked_at FROM displays_clicks ORDER BY page_id")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QStringLiteral("/en/page-0"));
        QCOMPARE(q.value(1).toString(), QStringLiteral("2026-08-30T10:00:00"));
        QCOMPARE(q.value(2).toString(), QStringLiteral("2026-08-30T10:01:00"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QStringLiteral("/en/page-1"));
        QVERIFY(q.value(2).isNull()); // NULL clicked_at survives the merge
    }
    QSqlDatabase::removeDatabase(connName);
}

void Test_Website_StatsDbMerger::test_merger_keeps_open_connections_valid()
{
    QTemporaryDir dir;
    const QString src  = dir.filePath(QStringLiteral("stats-en.db"));
    const QString dest = dir.filePath(QStringLiteral("stats.db"));
    createSourceDb(src, QStringLiteral("/en"), 2, 0);
    StatsDbMerger::merge({src}, dest);

    // Simulate PagesStatsWidget holding stats.db open across a re-download:
    // the merge must update the same file, not delete and recreate it.
    const QString connName = QStringLiteral("test_open_reader");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dest);
        QVERIFY(db.open());

        createSourceDb(dir.filePath(QStringLiteral("stats-fr.db")),
                       QStringLiteral("/fr"), 5, 0);
        StatsDbMerger::merge({src, dir.filePath(QStringLiteral("stats-fr.db"))}, dest);

        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM displays_clicks")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 7); // old connection sees the new merge
    }
    QSqlDatabase::removeDatabase(connName);
}

void Test_Website_StatsDbMerger::test_merger_missing_source_raises()
{
    QTemporaryDir dir;
    const QString dest = dir.filePath(QStringLiteral("stats.db"));

    bool raised = false;
    try {
        StatsDbMerger::merge({dir.filePath(QStringLiteral("does-not-exist.db"))}, dest);
    } catch (const ExceptionWithTitleText &) {
        raised = true;
    }
    QVERIFY(raised);
    // ATTACH must not have created the missing source as an empty file.
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("does-not-exist.db"))));
}

void Test_Website_StatsDbMerger::test_merger_empty_sources_wipe_destination()
{
    QTemporaryDir dir;
    const QString src  = dir.filePath(QStringLiteral("stats-en.db"));
    const QString dest = dir.filePath(QStringLiteral("stats.db"));
    createSourceDb(src, QStringLiteral("/en"), 3, 3);
    StatsDbMerger::merge({src}, dest);

    const StatsDbMerger::MergeResult result = StatsDbMerger::merge({}, dest);

    QCOMPARE(result.displaysClicks, 0);
    QCOMPARE(countRows(dest, QStringLiteral("displays_clicks")), 0);
    QCOMPARE(countRows(dest, QStringLiteral("page_session")), 0);
}

void Test_Website_StatsDbMerger::test_merger_preserves_is_bot_flag()
{
    QTemporaryDir dir;
    const QString src  = dir.filePath(QStringLiteral("stats-en.db"));
    const QString dest = dir.filePath(QStringLiteral("stats.db"));

    // Source with the NEW schema: 2 bot displays, 1 human display.
    const QString connName = QStringLiteral("test_bot_source");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(src);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE displays_clicks (id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " page_id TEXT NOT NULL, display_at TEXT NOT NULL, clicked_at TEXT,"
            " is_bot INTEGER NOT NULL DEFAULT 0)")));
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE page_session (id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " page_id TEXT NOT NULL, scrolling_percentage INTEGER NOT NULL,"
            " time_on_page INTEGER NOT NULL, is_final_page INTEGER NOT NULL,"
            " is_bot INTEGER NOT NULL DEFAULT 0)")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO displays_clicks (page_id, display_at, is_bot) VALUES"
            " ('/p', 't', 1), ('/p', 't', 1), ('/p', 't', 0)")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO page_session (page_id, scrolling_percentage, time_on_page,"
            " is_final_page, is_bot) VALUES ('/p', 50, 10, 1, 1)")));
    }
    QSqlDatabase::removeDatabase(connName);

    StatsDbMerger::merge({src}, dest);

    const QString countConn = QStringLiteral("test_bot_count");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), countConn);
        db.setDatabaseName(dest);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("SELECT SUM(is_bot), COUNT(*) FROM displays_clicks")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 2);
        QCOMPARE(q.value(1).toInt(), 3);
        QVERIFY(q.exec(QStringLiteral("SELECT SUM(is_bot) FROM page_session")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
    }
    QSqlDatabase::removeDatabase(countConn);
}

void Test_Website_StatsDbMerger::test_merger_legacy_source_without_is_bot_defaults_to_zero()
{
    QTemporaryDir dir;
    const QString src  = dir.filePath(QStringLiteral("stats-en.db"));
    const QString dest = dir.filePath(QStringLiteral("stats.db"));
    createSourceDb(src, QStringLiteral("/en"), 3, 2); // legacy schema, no is_bot

    StatsDbMerger::merge({src}, dest);

    QCOMPARE(countRows(dest, QStringLiteral("displays_clicks")), 3);
    const QString connName = QStringLiteral("test_legacy_bot");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dest);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("SELECT SUM(is_bot) FROM displays_clicks")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0); // unclassified rows count as human
    }
    QSqlDatabase::removeDatabase(connName);
}

QTEST_MAIN(Test_Website_StatsDbMerger)
#include "test_stats_db_merger.moc"
