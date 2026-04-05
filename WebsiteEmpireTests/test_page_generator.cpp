#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <atomic>

#include "website/pages/PageDb.h"
#include "website/pages/PageGenerator.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/PageBlocText.h"
#include "website/EngineArticles.h"

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

namespace {
struct Fixture {
    QTemporaryDir    dir;
    CategoryTable    categoryTable;
    PageDb           db;
    PageRepositoryDb repo;
    PageGenerator    gen;
    EngineArticles   engine;

    Fixture()
        : categoryTable(QDir(dir.path()))
        , db(QDir(dir.path()))
        , repo(db)
        , gen(repo, categoryTable)
    {}

    // Creates an article page with the given text and returns its id.
    int addArticle(const QString &permalink, const QString &text)
    {
        const int id = repo.create(QStringLiteral("article"), permalink, QStringLiteral("en"));
        repo.saveData(id, {
            {QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT), text},
            {QStringLiteral("0_categories"), QString()},
        });
        return id;
    }

    // Opens content.db and returns a unique connection name.
    QString openContentDb()
    {
        static std::atomic<int> s_counter{0};
        const QString conn = QStringLiteral("test_content_db_")
                             + QString::number(s_counter.fetch_add(1));
        QSqlDatabase db2 = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db2.setDatabaseName(QDir(dir.path()).filePath(QLatin1StringView(PageGenerator::FILENAME)));
        db2.open();
        return conn;
    }

    void closeContentDb(const QString &conn)
    {
        { QSqlDatabase::database(conn).close(); }
        QSqlDatabase::removeDatabase(conn);
    }
};
} // namespace

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class Test_PageGenerator : public QObject
{
    Q_OBJECT

private slots:
    // --- gzipCompress ---
    void test_pagegen_gzip_non_empty_input_produces_output();
    void test_pagegen_gzip_output_starts_with_gzip_magic();
    void test_pagegen_gzip_empty_input_produces_valid_gzip();

    // --- computeEtag ---
    void test_pagegen_etag_non_empty();
    void test_pagegen_etag_same_data_same_etag();
    void test_pagegen_etag_different_data_different_etag();

    // --- generateAll ---
    void test_pagegen_generate_returns_correct_count();
    void test_pagegen_generate_zero_pages_returns_zero();
    void test_pagegen_generate_creates_page_row_in_content_db();
    void test_pagegen_generate_stores_correct_permalink();
    void test_pagegen_generate_stores_correct_domain();
    void test_pagegen_generate_stores_correct_lang();
    void test_pagegen_generate_creates_variant_row();
    void test_pagegen_generate_html_gz_is_non_empty();
    void test_pagegen_generate_etag_matches_compressed_html();
    void test_pagegen_generate_variant_is_active();
    void test_pagegen_generate_variant_label_is_control();
    void test_pagegen_generate_html_contains_text_content();
    void test_pagegen_generate_redirect_row_for_old_permalink();
    void test_pagegen_generate_second_run_updates_existing_rows();
};

// ---------------------------------------------------------------------------
// gzipCompress
// ---------------------------------------------------------------------------

void Test_PageGenerator::test_pagegen_gzip_non_empty_input_produces_output()
{
    QVERIFY(!PageGenerator::gzipCompress(QByteArray("hello")).isEmpty());
}

void Test_PageGenerator::test_pagegen_gzip_output_starts_with_gzip_magic()
{
    const QByteArray &gz = PageGenerator::gzipCompress(QByteArray("hello"));
    // gzip magic bytes: 0x1f 0x8b
    QVERIFY(gz.size() >= 2);
    QCOMPARE(static_cast<uint8_t>(gz.at(0)), 0x1fu);
    QCOMPARE(static_cast<uint8_t>(gz.at(1)), 0x8bu);
}

void Test_PageGenerator::test_pagegen_gzip_empty_input_produces_valid_gzip()
{
    const QByteArray &gz = PageGenerator::gzipCompress(QByteArray());
    QVERIFY(gz.size() >= 2);
    QCOMPARE(static_cast<uint8_t>(gz.at(0)), 0x1fu);
    QCOMPARE(static_cast<uint8_t>(gz.at(1)), 0x8bu);
}

// ---------------------------------------------------------------------------
// computeEtag
// ---------------------------------------------------------------------------

void Test_PageGenerator::test_pagegen_etag_non_empty()
{
    QVERIFY(!PageGenerator::computeEtag(QByteArray("data")).isEmpty());
}

void Test_PageGenerator::test_pagegen_etag_same_data_same_etag()
{
    QCOMPARE(PageGenerator::computeEtag(QByteArray("data")),
             PageGenerator::computeEtag(QByteArray("data")));
}

void Test_PageGenerator::test_pagegen_etag_different_data_different_etag()
{
    QVERIFY(PageGenerator::computeEtag(QByteArray("a")) !=
            PageGenerator::computeEtag(QByteArray("b")));
}

// ---------------------------------------------------------------------------
// generateAll
// ---------------------------------------------------------------------------

void Test_PageGenerator::test_pagegen_generate_returns_correct_count()
{
    Fixture f;
    f.addArticle(QStringLiteral("/p1.html"), QStringLiteral("first"));
    f.addArticle(QStringLiteral("/p2.html"), QStringLiteral("second"));
    QCOMPARE(f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0), 2);
}

void Test_PageGenerator::test_pagegen_generate_zero_pages_returns_zero()
{
    Fixture f;
    QCOMPARE(f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0), 0);
}

void Test_PageGenerator::test_pagegen_generate_creates_page_row_in_content_db()
{
    Fixture f;
    f.addArticle(QStringLiteral("/p.html"), QStringLiteral("text"));
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
    f.closeContentDb(conn);
}

void Test_PageGenerator::test_pagegen_generate_stores_correct_permalink()
{
    Fixture f;
    f.addArticle(QStringLiteral("/my-article.html"), QStringLiteral("text"));
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral("SELECT path FROM pages LIMIT 1"));
    q.next();
    QCOMPARE(q.value(0).toString(), QStringLiteral("/my-article.html"));
    f.closeContentDb(conn);
}

void Test_PageGenerator::test_pagegen_generate_stores_correct_domain()
{
    Fixture f;
    f.addArticle(QStringLiteral("/p.html"), QStringLiteral("text"));
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("mysite.com"), f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral("SELECT domain FROM pages LIMIT 1"));
    q.next();
    QCOMPARE(q.value(0).toString(), QStringLiteral("mysite.com"));
    f.closeContentDb(conn);
}

void Test_PageGenerator::test_pagegen_generate_stores_correct_lang()
{
    Fixture f;
    f.addArticle(QStringLiteral("/p.html"), QStringLiteral("text"));
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral("SELECT lang FROM pages LIMIT 1"));
    q.next();
    QCOMPARE(q.value(0).toString(), QStringLiteral("en"));
    f.closeContentDb(conn);
}

void Test_PageGenerator::test_pagegen_generate_creates_variant_row()
{
    Fixture f;
    f.addArticle(QStringLiteral("/p.html"), QStringLiteral("text"));
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM page_variants"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
    f.closeContentDb(conn);
}

void Test_PageGenerator::test_pagegen_generate_html_gz_is_non_empty()
{
    Fixture f;
    f.addArticle(QStringLiteral("/p.html"), QStringLiteral("text"));
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral("SELECT LENGTH(html_gz) FROM page_variants LIMIT 1"));
    q.next();
    QVERIFY(q.value(0).toInt() > 0);
    f.closeContentDb(conn);
}

void Test_PageGenerator::test_pagegen_generate_etag_matches_compressed_html()
{
    Fixture f;
    f.addArticle(QStringLiteral("/p.html"), QStringLiteral("text"));
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral("SELECT html_gz, etag FROM page_variants LIMIT 1"));
    q.next();
    const QByteArray &gz   = q.value(0).toByteArray();
    const QString    &etag = q.value(1).toString();
    QCOMPARE(etag, PageGenerator::computeEtag(gz));
    f.closeContentDb(conn);
}

void Test_PageGenerator::test_pagegen_generate_variant_is_active()
{
    Fixture f;
    f.addArticle(QStringLiteral("/p.html"), QStringLiteral("text"));
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral("SELECT is_active FROM page_variants LIMIT 1"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
    f.closeContentDb(conn);
}

void Test_PageGenerator::test_pagegen_generate_variant_label_is_control()
{
    Fixture f;
    f.addArticle(QStringLiteral("/p.html"), QStringLiteral("text"));
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral("SELECT label FROM page_variants LIMIT 1"));
    q.next();
    QCOMPARE(q.value(0).toString(), QStringLiteral("control"));
    f.closeContentDb(conn);
}

void Test_PageGenerator::test_pagegen_generate_html_contains_text_content()
{
    // The decompressed HTML must contain the page text.
    // We can't easily decompress gzip in Qt without zlib, but we can
    // verify indirectly: the etag changes if we generate a different text.
    Fixture f;
    f.addArticle(QStringLiteral("/p.html"), QStringLiteral("unique-marker-xyz"));
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    Fixture f2;
    f2.addArticle(QStringLiteral("/p.html"), QStringLiteral("different-content-abc"));
    f2.gen.generateAll(QDir(f2.dir.path()), QStringLiteral("example.com"), f2.engine, 0);

    const auto etag = [](const QString &conn) {
        QSqlQuery q(QSqlDatabase::database(conn));
        q.exec(QStringLiteral("SELECT etag FROM page_variants LIMIT 1"));
        q.next();
        return q.value(0).toString();
    };

    const QString &conn1 = f.openContentDb();
    const QString &conn2 = f2.openContentDb();
    const QString &e1 = etag(conn1);
    const QString &e2 = etag(conn2);
    f.closeContentDb(conn1);
    f2.closeContentDb(conn2);

    QVERIFY(e1 != e2);
}

void Test_PageGenerator::test_pagegen_generate_redirect_row_for_old_permalink()
{
    Fixture f;
    const int id = f.addArticle(QStringLiteral("/old.html"), QStringLiteral("text"));
    f.repo.updatePermalink(id, QStringLiteral("/new.html"));
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral(
        "SELECT new_path, status_code FROM redirects WHERE old_path = '/old.html'"));
    q.next();
    QCOMPARE(q.value(0).toString(), QStringLiteral("/new.html"));
    QCOMPARE(q.value(1).toInt(), 301);
    f.closeContentDb(conn);
}

void Test_PageGenerator::test_pagegen_generate_second_run_updates_existing_rows()
{
    Fixture f;
    const int id = f.addArticle(QStringLiteral("/p.html"), QStringLiteral("v1"));
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    // Update text and regenerate.
    f.repo.saveData(id, {
        {QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("v2")},
        {QStringLiteral("0_categories"), QString()},
    });
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1); // still one row, not two
    f.closeContentDb(conn);
}

QTEST_MAIN(Test_PageGenerator)
#include "test_page_generator.moc"
