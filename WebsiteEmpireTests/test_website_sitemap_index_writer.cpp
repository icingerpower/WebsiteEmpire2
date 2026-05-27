#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <atomic>

#include "website/sitemap/SitemapIndexWriter.h"
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
        : connName(QStringLiteral("test_sitemap_index_")
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

    QList<SitemapIndexEntry> twoEntries()
    {
        QList<SitemapIndexEntry> entries;
        entries << SitemapIndexEntry{
            QStringLiteral("https://example.com/sitemap-en-1.xml"),
            QStringLiteral("2024-03-15")};
        entries << SitemapIndexEntry{
            QStringLiteral("https://example.com/sitemap-fr-1.xml"),
            QStringLiteral("2024-02-01")};
        return entries;
    }
};

} // namespace

// =============================================================================
// Test_Website_SitemapIndexWriter
// =============================================================================

class Test_Website_SitemapIndexWriter : public QObject
{
    Q_OBJECT

private slots:
    // --- DB writes ---
    void test_index_write_creates_page_row();
    void test_index_write_stores_lang_sentinel();
    void test_index_write_stores_domain();
    void test_index_write_creates_variant_row();
    void test_index_write_variant_blob_non_empty();
    void test_index_write_variant_label_is_control();
    void test_index_write_variant_is_active();

    // --- idempotency ---
    void test_index_write_upsert_creates_one_page_row();
    void test_index_write_upsert_creates_one_variant_row();

    // --- content ---
    void test_index_write_empty_entries_still_creates_page();
};

// =============================================================================
// DB writes
// =============================================================================

void Test_Website_SitemapIndexWriter::test_index_write_creates_page_row()
{
    Fixture f;
    SitemapIndexWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("https://example.com"), f.twoEntries());

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE path = '/sitemap.xml'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
}

void Test_Website_SitemapIndexWriter::test_index_write_stores_lang_sentinel()
{
    Fixture f;
    SitemapIndexWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("https://example.com"), f.twoEntries());

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral("SELECT lang FROM pages WHERE path = '/sitemap.xml'"));
    q.next();
    QCOMPARE(q.value(0).toString(), QStringLiteral("_"));
}

void Test_Website_SitemapIndexWriter::test_index_write_stores_domain()
{
    Fixture f;
    SitemapIndexWriter::write(f.connName, QStringLiteral("mysite.com"),
                              QStringLiteral("https://mysite.com"), f.twoEntries());

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral("SELECT domain FROM pages WHERE path = '/sitemap.xml'"));
    q.next();
    QCOMPARE(q.value(0).toString(), QStringLiteral("mysite.com"));
}

void Test_Website_SitemapIndexWriter::test_index_write_creates_variant_row()
{
    Fixture f;
    SitemapIndexWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("https://example.com"), f.twoEntries());

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/sitemap.xml'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
}

void Test_Website_SitemapIndexWriter::test_index_write_variant_blob_non_empty()
{
    Fixture f;
    SitemapIndexWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("https://example.com"), f.twoEntries());

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral(
        "SELECT LENGTH(pv.html_gz) FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/sitemap.xml'"));
    q.next();
    QVERIFY(q.value(0).toInt() > 0);
}

void Test_Website_SitemapIndexWriter::test_index_write_variant_label_is_control()
{
    Fixture f;
    SitemapIndexWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("https://example.com"), f.twoEntries());

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral(
        "SELECT pv.label FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/sitemap.xml'"));
    q.next();
    QCOMPARE(q.value(0).toString(), QStringLiteral("control"));
}

void Test_Website_SitemapIndexWriter::test_index_write_variant_is_active()
{
    Fixture f;
    SitemapIndexWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("https://example.com"), f.twoEntries());

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral(
        "SELECT pv.is_active FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/sitemap.xml'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
}

// =============================================================================
// Idempotency
// =============================================================================

void Test_Website_SitemapIndexWriter::test_index_write_upsert_creates_one_page_row()
{
    Fixture f;
    SitemapIndexWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("https://example.com"), f.twoEntries());
    SitemapIndexWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("https://example.com"), f.twoEntries());

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE path = '/sitemap.xml'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
}

void Test_Website_SitemapIndexWriter::test_index_write_upsert_creates_one_variant_row()
{
    Fixture f;
    SitemapIndexWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("https://example.com"), f.twoEntries());
    SitemapIndexWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("https://example.com"), f.twoEntries());

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/sitemap.xml'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
}

// =============================================================================
// Content
// =============================================================================

void Test_Website_SitemapIndexWriter::test_index_write_empty_entries_still_creates_page()
{
    Fixture f;
    SitemapIndexWriter::write(f.connName, QStringLiteral("example.com"),
                              QStringLiteral("https://example.com"), {});

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE path = '/sitemap.xml'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
}

QTEST_MAIN(Test_Website_SitemapIndexWriter)
#include "test_website_sitemap_index_writer.moc"
