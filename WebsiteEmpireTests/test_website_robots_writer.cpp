#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <atomic>

#include "website/sitemap/RobotsWriter.h"

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
        : connName(QStringLiteral("test_robots_writer_")
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

void insertPage(const QString &connName, const QString &path, const QString &lang = QStringLiteral("en"))
{
    QSqlQuery q(QSqlDatabase::database(connName));
    q.prepare(QStringLiteral(
        "INSERT INTO pages (path, domain, lang, etag, updated_at)"
        " VALUES (:path, 'x.com', :lang, 'e', '2026-01-01')"));
    q.bindValue(QStringLiteral(":path"), path);
    q.bindValue(QStringLiteral(":lang"), lang);
    q.exec();
}

} // namespace

// =============================================================================
// Test_Website_RobotsWriter
// =============================================================================

class Test_Website_RobotsWriter : public QObject
{
    Q_OBJECT

private slots:
    // --- DB writes ---
    void test_robots_write_creates_page_row();
    void test_robots_write_stores_lang_sentinel();
    void test_robots_write_stores_domain();
    void test_robots_write_creates_variant_row();
    void test_robots_write_variant_blob_non_empty();
    void test_robots_write_variant_label_is_control();
    void test_robots_write_variant_is_active();

    // --- idempotency ---
    void test_robots_write_upsert_creates_one_page_row();
    void test_robots_write_upsert_creates_one_variant_row();

    // --- Disallow: legal pages ---
    void test_robots_disallow_emitted_when_legal_page_exists();
    void test_robots_disallow_not_emitted_when_legal_page_absent();
    void test_robots_disallow_skips_sentinel_lang_rows();
};

// =============================================================================
// DB writes
// =============================================================================

void Test_Website_RobotsWriter::test_robots_write_creates_page_row()
{
    Fixture f;
    RobotsWriter::write(f.connName, QStringLiteral("example.com"),
                        QStringLiteral("https://example.com"));

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE path = '/robots.txt'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
}

void Test_Website_RobotsWriter::test_robots_write_stores_lang_sentinel()
{
    Fixture f;
    RobotsWriter::write(f.connName, QStringLiteral("example.com"),
                        QStringLiteral("https://example.com"));

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral("SELECT lang FROM pages WHERE path = '/robots.txt'"));
    q.next();
    QCOMPARE(q.value(0).toString(), QStringLiteral("_"));
}

void Test_Website_RobotsWriter::test_robots_write_stores_domain()
{
    Fixture f;
    RobotsWriter::write(f.connName, QStringLiteral("mysite.com"),
                        QStringLiteral("https://mysite.com"));

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral("SELECT domain FROM pages WHERE path = '/robots.txt'"));
    q.next();
    QCOMPARE(q.value(0).toString(), QStringLiteral("mysite.com"));
}

void Test_Website_RobotsWriter::test_robots_write_creates_variant_row()
{
    Fixture f;
    RobotsWriter::write(f.connName, QStringLiteral("example.com"),
                        QStringLiteral("https://example.com"));

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/robots.txt'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
}

void Test_Website_RobotsWriter::test_robots_write_variant_blob_non_empty()
{
    Fixture f;
    RobotsWriter::write(f.connName, QStringLiteral("example.com"),
                        QStringLiteral("https://example.com"));

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral(
        "SELECT LENGTH(pv.html_gz) FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/robots.txt'"));
    q.next();
    QVERIFY(q.value(0).toInt() > 0);
}

void Test_Website_RobotsWriter::test_robots_write_variant_label_is_control()
{
    Fixture f;
    RobotsWriter::write(f.connName, QStringLiteral("example.com"),
                        QStringLiteral("https://example.com"));

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral(
        "SELECT pv.label FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/robots.txt'"));
    q.next();
    QCOMPARE(q.value(0).toString(), QStringLiteral("control"));
}

void Test_Website_RobotsWriter::test_robots_write_variant_is_active()
{
    Fixture f;
    RobotsWriter::write(f.connName, QStringLiteral("example.com"),
                        QStringLiteral("https://example.com"));

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral(
        "SELECT pv.is_active FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/robots.txt'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
}

// =============================================================================
// Idempotency
// =============================================================================

void Test_Website_RobotsWriter::test_robots_write_upsert_creates_one_page_row()
{
    Fixture f;
    RobotsWriter::write(f.connName, QStringLiteral("example.com"),
                        QStringLiteral("https://example.com"));
    RobotsWriter::write(f.connName, QStringLiteral("example.com"),
                        QStringLiteral("https://example.com"));

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE path = '/robots.txt'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
}

void Test_Website_RobotsWriter::test_robots_write_upsert_creates_one_variant_row()
{
    Fixture f;
    RobotsWriter::write(f.connName, QStringLiteral("example.com"),
                        QStringLiteral("https://example.com"));
    RobotsWriter::write(f.connName, QStringLiteral("example.com"),
                        QStringLiteral("https://example.com"));

    QSqlQuery q(QSqlDatabase::database(f.connName));
    q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/robots.txt'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
}

// =============================================================================
// Disallow: legal pages
// =============================================================================

void Test_Website_RobotsWriter::test_robots_disallow_emitted_when_legal_page_exists()
{
    // DB with two legal pages → blob must be larger than baseline (no legal pages).
    Fixture fWith;
    insertPage(fWith.connName, QStringLiteral("/privacy-policy.html"));
    insertPage(fWith.connName, QStringLiteral("/contact-us.html"));
    RobotsWriter::write(fWith.connName, QStringLiteral("example.com"),
                        QStringLiteral("https://example.com"));

    Fixture fWithout;
    RobotsWriter::write(fWithout.connName, QStringLiteral("example.com"),
                        QStringLiteral("https://example.com"));

    auto blobSize = [](const QString &connName) {
        QSqlQuery q(QSqlDatabase::database(connName));
        q.exec(QStringLiteral(
            "SELECT LENGTH(pv.html_gz) FROM page_variants pv"
            " JOIN pages p ON pv.page_id = p.id WHERE p.path = '/robots.txt'"));
        q.next();
        return q.value(0).toInt();
    };

    QVERIFY(blobSize(fWith.connName) > blobSize(fWithout.connName));
}

void Test_Website_RobotsWriter::test_robots_disallow_not_emitted_when_legal_page_absent()
{
    Fixture f;
    // No legal pages inserted — only a regular content page exists.
    insertPage(f.connName, QStringLiteral("/some-article"));
    RobotsWriter::write(f.connName, QStringLiteral("example.com"),
                        QStringLiteral("https://example.com"));

    // Blob must equal the baseline (no legal pages anywhere) — same size check.
    Fixture fBaseline;
    RobotsWriter::write(fBaseline.connName, QStringLiteral("example.com"),
                        QStringLiteral("https://example.com"));

    QSqlQuery q1(QSqlDatabase::database(f.connName));
    q1.exec(QStringLiteral(
        "SELECT LENGTH(pv.html_gz) FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id WHERE p.path = '/robots.txt'"));
    q1.next();

    QSqlQuery q2(QSqlDatabase::database(fBaseline.connName));
    q2.exec(QStringLiteral(
        "SELECT LENGTH(pv.html_gz) FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id WHERE p.path = '/robots.txt'"));
    q2.next();

    QCOMPARE(q1.value(0).toInt(), q2.value(0).toInt());
}

void Test_Website_RobotsWriter::test_robots_disallow_skips_sentinel_lang_rows()
{
    Fixture f;
    // Insert /privacy-policy.html with lang='_' (sentinel) — must NOT trigger Disallow.
    insertPage(f.connName, QStringLiteral("/privacy-policy.html"), QStringLiteral("_"));
    RobotsWriter::write(f.connName, QStringLiteral("example.com"),
                        QStringLiteral("https://example.com"));

    Fixture fBaseline;
    RobotsWriter::write(fBaseline.connName, QStringLiteral("example.com"),
                        QStringLiteral("https://example.com"));

    QSqlQuery q1(QSqlDatabase::database(f.connName));
    q1.exec(QStringLiteral(
        "SELECT LENGTH(pv.html_gz) FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id WHERE p.path = '/robots.txt'"));
    q1.next();

    QSqlQuery q2(QSqlDatabase::database(fBaseline.connName));
    q2.exec(QStringLiteral(
        "SELECT LENGTH(pv.html_gz) FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id WHERE p.path = '/robots.txt'"));
    q2.next();

    QCOMPARE(q1.value(0).toInt(), q2.value(0).toInt());
}

QTEST_MAIN(Test_Website_RobotsWriter)
#include "test_website_robots_writer.moc"
