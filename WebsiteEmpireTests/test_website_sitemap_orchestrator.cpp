#include <QtTest>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <atomic>

#include "website/sitemap/SitemapConfig.h"
#include "website/sitemap/SitemapOrchestrator.h"

// =============================================================================
// Helpers
// =============================================================================

namespace {

static std::atomic<int> s_connCounter{0};

void ensureSchema(const QString &connName)
{
    QSqlQuery q(QSqlDatabase::database(connName));
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS pages ("
        "  id         INTEGER PRIMARY KEY,"
        "  path       TEXT    UNIQUE NOT NULL,"
        "  domain     TEXT    NOT NULL,"
        "  lang       TEXT    NOT NULL,"
        "  etag       TEXT    NOT NULL,"
        "  updated_at TEXT    NOT NULL"
        ")"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS page_variants ("
        "  id        INTEGER PRIMARY KEY,"
        "  page_id   INTEGER NOT NULL REFERENCES pages(id),"
        "  label     TEXT    NOT NULL,"
        "  is_active INTEGER NOT NULL,"
        "  html_gz   BLOB    NOT NULL,"
        "  etag      TEXT    NOT NULL,"
        "  UNIQUE(page_id, label)"
        ")"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS redirects ("
        "  old_path    TEXT    PRIMARY KEY,"
        "  new_path    TEXT,"
        "  status_code INTEGER NOT NULL"
        ")"));
}

void insertPage(const QString &connName, const QString &path,
                const QString &lang, const QString &updatedAt)
{
    QSqlQuery q(QSqlDatabase::database(connName));
    q.prepare(QStringLiteral(
        "INSERT INTO pages (path, domain, lang, etag, updated_at)"
        " VALUES (:path, 'example.com', :lang, 'etag', :updated_at)"));
    q.bindValue(QStringLiteral(":path"),       path);
    q.bindValue(QStringLiteral(":lang"),       lang);
    q.bindValue(QStringLiteral(":updated_at"), updatedAt);
    q.exec();
}

struct Fixture {
    QTemporaryDir dir;
    QString       connName;

    Fixture()
        : connName(QStringLiteral("test_sitemap_orch_")
                   + QString::number(s_connCounter.fetch_add(1)))
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dir.filePath(QStringLiteral("content.db")));
        db.open();
        ensureSchema(connName);
    }

    ~Fixture()
    {
        {
            QSqlDatabase::database(connName).close();
        }
        QSqlDatabase::removeDatabase(connName);
    }

    int countPages(const QString &pathLike = QString()) const
    {
        QSqlQuery q(QSqlDatabase::database(connName));
        if (pathLike.isEmpty()) {
            q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE lang = '_'"));
        } else {
            q.prepare(QStringLiteral("SELECT COUNT(*) FROM pages WHERE path LIKE :p"));
            q.bindValue(QStringLiteral(":p"), pathLike);
            q.exec();
        }
        q.next();
        return q.value(0).toInt();
    }

    bool hasPath(const QString &path) const
    {
        QSqlQuery q(QSqlDatabase::database(connName));
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM pages WHERE path = :p"));
        q.bindValue(QStringLiteral(":p"), path);
        q.exec();
        q.next();
        return q.value(0).toInt() == 1;
    }
};

SitemapConfig configChunkSize(int size)
{
    SitemapConfig cfg;
    cfg.chunkSize  = size;
    cfg.recentDays = 0; // cutoff = today; old test dates are never "recent"
    return cfg;
}

} // namespace

// =============================================================================
// Test_Website_SitemapOrchestrator
// =============================================================================

class Test_Website_SitemapOrchestrator : public QObject
{
    Q_OBJECT

private slots:
    // --- early exit ---
    void test_orch_empty_base_url_is_noop();

    // --- basic generation ---
    void test_orch_creates_sitemap_index();
    void test_orch_creates_robots_txt();
    void test_orch_creates_lang_chunk_for_single_lang();
    void test_orch_creates_two_chunks_when_chunk_size_is_one();
    void test_orch_creates_chunks_for_each_lang();

    // --- sentinel lang ---
    void test_orch_sitemap_pages_have_lang_sentinel();
    void test_orch_content_pages_are_not_modified();

    // --- stale cleanup ---
    void test_orch_second_run_removes_extra_chunk();

    // --- recent entries ---
    void test_orch_recent_sitemap_created_when_recent_days_covers_entries();
    void test_orch_no_recent_sitemap_when_no_entries_qualify();

    // --- empty DB ---
    void test_orch_empty_db_creates_index_and_robots_only();
};

// =============================================================================
// Early exit
// =============================================================================

void Test_Website_SitemapOrchestrator::test_orch_empty_base_url_is_noop()
{
    Fixture f;
    insertPage(f.connName, QStringLiteral("/p.html"), QStringLiteral("en"),
               QStringLiteral("2024-01-01"));

    SitemapOrchestrator::generate(f.connName, QStringLiteral("example.com"),
                                  QString(), configChunkSize(50000));

    QCOMPARE(f.countPages(), 0); // no sitemap/robots rows written
}

// =============================================================================
// Basic generation
// =============================================================================

void Test_Website_SitemapOrchestrator::test_orch_creates_sitemap_index()
{
    Fixture f;
    insertPage(f.connName, QStringLiteral("/p.html"), QStringLiteral("en"),
               QStringLiteral("2024-01-01"));

    SitemapOrchestrator::generate(f.connName, QStringLiteral("example.com"),
                                  QStringLiteral("https://example.com"),
                                  configChunkSize(50000));

    QVERIFY(f.hasPath(QStringLiteral("/sitemap.xml")));
}

void Test_Website_SitemapOrchestrator::test_orch_creates_robots_txt()
{
    Fixture f;
    insertPage(f.connName, QStringLiteral("/p.html"), QStringLiteral("en"),
               QStringLiteral("2024-01-01"));

    SitemapOrchestrator::generate(f.connName, QStringLiteral("example.com"),
                                  QStringLiteral("https://example.com"),
                                  configChunkSize(50000));

    QVERIFY(f.hasPath(QStringLiteral("/robots.txt")));
}

void Test_Website_SitemapOrchestrator::test_orch_creates_lang_chunk_for_single_lang()
{
    Fixture f;
    insertPage(f.connName, QStringLiteral("/p.html"), QStringLiteral("en"),
               QStringLiteral("2024-01-01"));

    SitemapOrchestrator::generate(f.connName, QStringLiteral("example.com"),
                                  QStringLiteral("https://example.com"),
                                  configChunkSize(50000));

    QVERIFY(f.hasPath(QStringLiteral("/sitemap-en-1.xml")));
}

void Test_Website_SitemapOrchestrator::test_orch_creates_two_chunks_when_chunk_size_is_one()
{
    Fixture f;
    insertPage(f.connName, QStringLiteral("/p1.html"), QStringLiteral("en"),
               QStringLiteral("2024-01-01"));
    insertPage(f.connName, QStringLiteral("/p2.html"), QStringLiteral("en"),
               QStringLiteral("2024-01-02"));

    SitemapOrchestrator::generate(f.connName, QStringLiteral("example.com"),
                                  QStringLiteral("https://example.com"),
                                  configChunkSize(1));

    QVERIFY(f.hasPath(QStringLiteral("/sitemap-en-1.xml")));
    QVERIFY(f.hasPath(QStringLiteral("/sitemap-en-2.xml")));
}

void Test_Website_SitemapOrchestrator::test_orch_creates_chunks_for_each_lang()
{
    Fixture f;
    insertPage(f.connName, QStringLiteral("/en/p.html"), QStringLiteral("en"),
               QStringLiteral("2024-01-01"));
    insertPage(f.connName, QStringLiteral("/fr/p.html"), QStringLiteral("fr"),
               QStringLiteral("2024-01-02"));

    SitemapOrchestrator::generate(f.connName, QStringLiteral("example.com"),
                                  QStringLiteral("https://example.com"),
                                  configChunkSize(50000));

    QVERIFY(f.hasPath(QStringLiteral("/sitemap-en-1.xml")));
    QVERIFY(f.hasPath(QStringLiteral("/sitemap-fr-1.xml")));
}

// =============================================================================
// Sentinel lang
// =============================================================================

void Test_Website_SitemapOrchestrator::test_orch_sitemap_pages_have_lang_sentinel()
{
    Fixture f;
    insertPage(f.connName, QStringLiteral("/p.html"), QStringLiteral("en"),
               QStringLiteral("2024-01-01"));

    SitemapOrchestrator::generate(f.connName, QStringLiteral("example.com"),
                                  QStringLiteral("https://example.com"),
                                  configChunkSize(50000));

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM pages WHERE lang != '_' AND lang != 'en'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 0); // only 'en' and '_' rows
}

void Test_Website_SitemapOrchestrator::test_orch_content_pages_are_not_modified()
{
    Fixture f;
    insertPage(f.connName, QStringLiteral("/p.html"), QStringLiteral("en"),
               QStringLiteral("2024-01-01"));

    SitemapOrchestrator::generate(f.connName, QStringLiteral("example.com"),
                                  QStringLiteral("https://example.com"),
                                  configChunkSize(50000));

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE lang = 'en'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1); // original content page unchanged
}

// =============================================================================
// Stale cleanup
// =============================================================================

void Test_Website_SitemapOrchestrator::test_orch_second_run_removes_extra_chunk()
{
    Fixture f;
    // First run: 2 EN pages → 2 chunks (chunkSize=1)
    insertPage(f.connName, QStringLiteral("/p1.html"), QStringLiteral("en"),
               QStringLiteral("2024-01-01"));
    insertPage(f.connName, QStringLiteral("/p2.html"), QStringLiteral("en"),
               QStringLiteral("2024-01-02"));

    SitemapOrchestrator::generate(f.connName, QStringLiteral("example.com"),
                                  QStringLiteral("https://example.com"),
                                  configChunkSize(1));
    QVERIFY(f.hasPath(QStringLiteral("/sitemap-en-2.xml")));

    // Delete one content page then regenerate — only 1 chunk should remain.
    {
        QSqlQuery del(QSqlDatabase::database(f.connName));
        del.exec(QStringLiteral("DELETE FROM pages WHERE path = '/p2.html'"));
    }

    SitemapOrchestrator::generate(f.connName, QStringLiteral("example.com"),
                                  QStringLiteral("https://example.com"),
                                  configChunkSize(1));

    QVERIFY(f.hasPath(QStringLiteral("/sitemap-en-1.xml")));
    QVERIFY(!f.hasPath(QStringLiteral("/sitemap-en-2.xml")));
}

// =============================================================================
// Recent entries
// =============================================================================

void Test_Website_SitemapOrchestrator::test_orch_recent_sitemap_created_when_recent_days_covers_entries()
{
    Fixture f;
    const QString today = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd"));
    insertPage(f.connName, QStringLiteral("/p.html"), QStringLiteral("en"), today);

    SitemapConfig cfg;
    cfg.chunkSize  = 50000;
    cfg.recentDays = 7; // today's date is within 7 days
    SitemapOrchestrator::generate(f.connName, QStringLiteral("example.com"),
                                  QStringLiteral("https://example.com"), cfg);

    QVERIFY(f.hasPath(QStringLiteral("/sitemap-recent.xml")));
}

void Test_Website_SitemapOrchestrator::test_orch_no_recent_sitemap_when_no_entries_qualify()
{
    Fixture f;
    // Old date — never within any reasonable recentDays window.
    insertPage(f.connName, QStringLiteral("/p.html"), QStringLiteral("en"),
               QStringLiteral("2020-01-01"));

    SitemapConfig cfg;
    cfg.chunkSize  = 50000;
    cfg.recentDays = 7;
    SitemapOrchestrator::generate(f.connName, QStringLiteral("example.com"),
                                  QStringLiteral("https://example.com"), cfg);

    QVERIFY(!f.hasPath(QStringLiteral("/sitemap-recent.xml")));
}

// =============================================================================
// Empty DB
// =============================================================================

void Test_Website_SitemapOrchestrator::test_orch_empty_db_creates_index_and_robots_only()
{
    Fixture f;
    SitemapOrchestrator::generate(f.connName, QStringLiteral("example.com"),
                                  QStringLiteral("https://example.com"),
                                  configChunkSize(50000));

    QVERIFY(f.hasPath(QStringLiteral("/sitemap.xml")));
    QVERIFY(f.hasPath(QStringLiteral("/robots.txt")));
    // No lang-specific chunks (no content pages).
    QVERIFY(!f.hasPath(QStringLiteral("/sitemap-en-1.xml")));
}

QTEST_MAIN(Test_Website_SitemapOrchestrator)
#include "test_website_sitemap_orchestrator.moc"
