#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <atomic>

#include "website/sitemap/SitemapChunkWriter.h"
#include "website/sitemap/SitemapEntry.h"

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

struct Fixture {
    QTemporaryDir dir;
    QString       connName;

    Fixture()
        : connName(QStringLiteral("test_sitemap_chunk_")
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
};

} // namespace

// =============================================================================
// Test_Website_SitemapChunkWriter
// =============================================================================

class Test_Website_SitemapChunkWriter : public QObject
{
    Q_OBJECT

private slots:
    // --- DB writes ---
    void test_chunk_write_creates_page_row();
    void test_chunk_write_stores_lang_sentinel();
    void test_chunk_write_stores_domain();
    void test_chunk_write_creates_variant_row();
    void test_chunk_write_variant_blob_non_empty();
    void test_chunk_write_variant_label_is_control();
    void test_chunk_write_variant_is_active();

    // --- return value ---
    void test_chunk_write_returns_max_lastmod();
    void test_chunk_write_returns_max_lastmod_single_entry();
    void test_chunk_write_returns_empty_lastmod_for_empty_entries();

    // --- idempotency ---
    void test_chunk_write_upsert_creates_one_page_row();
    void test_chunk_write_upsert_creates_one_variant_row();
};

// =============================================================================
// DB writes
// =============================================================================

void Test_Website_SitemapChunkWriter::test_chunk_write_creates_page_row()
{
    Fixture f;
    QList<SitemapEntry> entries;
    entries << SitemapEntry{QStringLiteral("https://example.com/p.html"), QStringLiteral("2024-01-15")};

    SitemapChunkWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("/sitemap-en-1.xml"), entries);

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE path = '/sitemap-en-1.xml'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
}

void Test_Website_SitemapChunkWriter::test_chunk_write_stores_lang_sentinel()
{
    Fixture f;
    QList<SitemapEntry> entries;
    entries << SitemapEntry{QStringLiteral("https://example.com/p.html"), QStringLiteral("2024-01-15")};

    SitemapChunkWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("/sitemap-en-1.xml"), entries);

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral("SELECT lang FROM pages WHERE path = '/sitemap-en-1.xml'"));
    q.next();
    QCOMPARE(q.value(0).toString(), QStringLiteral("_"));
}

void Test_Website_SitemapChunkWriter::test_chunk_write_stores_domain()
{
    Fixture f;
    QList<SitemapEntry> entries;
    entries << SitemapEntry{QStringLiteral("https://mysite.com/p.html"), QStringLiteral("2024-01-15")};

    SitemapChunkWriter::write(f.connName, QStringLiteral("mysite.com"),
                              QStringLiteral("/sitemap-en-1.xml"), entries);

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral("SELECT domain FROM pages WHERE path = '/sitemap-en-1.xml'"));
    q.next();
    QCOMPARE(q.value(0).toString(), QStringLiteral("mysite.com"));
}

void Test_Website_SitemapChunkWriter::test_chunk_write_creates_variant_row()
{
    Fixture f;
    QList<SitemapEntry> entries;
    entries << SitemapEntry{QStringLiteral("https://example.com/p.html"), QStringLiteral("2024-01-15")};

    SitemapChunkWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("/sitemap-en-1.xml"), entries);

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/sitemap-en-1.xml'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
}

void Test_Website_SitemapChunkWriter::test_chunk_write_variant_blob_non_empty()
{
    Fixture f;
    QList<SitemapEntry> entries;
    entries << SitemapEntry{QStringLiteral("https://example.com/p.html"), QStringLiteral("2024-01-15")};

    SitemapChunkWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("/sitemap-en-1.xml"), entries);

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral(
        "SELECT LENGTH(pv.html_gz) FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/sitemap-en-1.xml'"));
    q.next();
    QVERIFY(q.value(0).toInt() > 0);
}

void Test_Website_SitemapChunkWriter::test_chunk_write_variant_label_is_control()
{
    Fixture f;
    QList<SitemapEntry> entries;
    entries << SitemapEntry{QStringLiteral("https://example.com/p.html"), QStringLiteral("2024-01-15")};

    SitemapChunkWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("/sitemap-en-1.xml"), entries);

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral(
        "SELECT pv.label FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/sitemap-en-1.xml'"));
    q.next();
    QCOMPARE(q.value(0).toString(), QStringLiteral("control"));
}

void Test_Website_SitemapChunkWriter::test_chunk_write_variant_is_active()
{
    Fixture f;
    QList<SitemapEntry> entries;
    entries << SitemapEntry{QStringLiteral("https://example.com/p.html"), QStringLiteral("2024-01-15")};

    SitemapChunkWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("/sitemap-en-1.xml"), entries);

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral(
        "SELECT pv.is_active FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/sitemap-en-1.xml'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
}

// =============================================================================
// Return value
// =============================================================================

void Test_Website_SitemapChunkWriter::test_chunk_write_returns_max_lastmod()
{
    Fixture f;
    QList<SitemapEntry> entries;
    entries << SitemapEntry{QStringLiteral("https://example.com/p1.html"), QStringLiteral("2024-01-15")};
    entries << SitemapEntry{QStringLiteral("https://example.com/p2.html"), QStringLiteral("2024-03-20")};
    entries << SitemapEntry{QStringLiteral("https://example.com/p3.html"), QStringLiteral("2024-02-01")};

    const QString maxLastmod = SitemapChunkWriter::write(
        f.connName, QStringLiteral("example.com"),
        QStringLiteral("/sitemap-en-1.xml"), entries);

    QCOMPARE(maxLastmod, QStringLiteral("2024-03-20"));
}

void Test_Website_SitemapChunkWriter::test_chunk_write_returns_max_lastmod_single_entry()
{
    Fixture f;
    QList<SitemapEntry> entries;
    entries << SitemapEntry{QStringLiteral("https://example.com/p.html"), QStringLiteral("2025-06-01")};

    const QString maxLastmod = SitemapChunkWriter::write(
        f.connName, QStringLiteral("example.com"),
        QStringLiteral("/sitemap-en-1.xml"), entries);

    QCOMPARE(maxLastmod, QStringLiteral("2025-06-01"));
}

void Test_Website_SitemapChunkWriter::test_chunk_write_returns_empty_lastmod_for_empty_entries()
{
    Fixture f;
    const QString maxLastmod = SitemapChunkWriter::write(
        f.connName, QStringLiteral("example.com"),
        QStringLiteral("/sitemap-en-1.xml"), {});

    QVERIFY(maxLastmod.isEmpty());
}

// =============================================================================
// Idempotency
// =============================================================================

void Test_Website_SitemapChunkWriter::test_chunk_write_upsert_creates_one_page_row()
{
    Fixture f;
    QList<SitemapEntry> entries;
    entries << SitemapEntry{QStringLiteral("https://example.com/p.html"), QStringLiteral("2024-01-15")};

    SitemapChunkWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("/sitemap-en-1.xml"), entries);
    SitemapChunkWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("/sitemap-en-1.xml"), entries);

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE path = '/sitemap-en-1.xml'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
}

void Test_Website_SitemapChunkWriter::test_chunk_write_upsert_creates_one_variant_row()
{
    Fixture f;
    QList<SitemapEntry> entries;
    entries << SitemapEntry{QStringLiteral("https://example.com/p.html"), QStringLiteral("2024-01-15")};

    SitemapChunkWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("/sitemap-en-1.xml"), entries);
    SitemapChunkWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("/sitemap-en-1.xml"), entries);

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/sitemap-en-1.xml'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
}

QTEST_MAIN(Test_Website_SitemapChunkWriter)
#include "test_website_sitemap_chunk_writer.moc"
