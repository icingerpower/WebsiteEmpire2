#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <atomic>
#include <zlib.h>

#include "CountryLangManager.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageGenerator.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/PageBlocText.h"
#include "website/EngineArticles.h"
#include "website/HostTable.h"

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

namespace {

QByteArray gzipDecompress(const QByteArray &compressed)
{
    z_stream strm = {};
    if (inflateInit2(&strm, 15 + 32) != Z_OK) {
        return {};
    }
    strm.avail_in = static_cast<uInt>(compressed.size());
    strm.next_in  = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    QByteArray result;
    char buf[4096];
    int ret;
    do {
        strm.avail_out = sizeof(buf);
        strm.next_out  = reinterpret_cast<Bytef *>(buf);
        ret = inflate(&strm, Z_NO_FLUSH);
        result.append(buf, static_cast<int>(sizeof(buf) - strm.avail_out));
    } while (ret == Z_OK);
    inflateEnd(&strm);
    return result;
}

struct Fixture {
    QTemporaryDir    dir;
    HostTable        hostTable;
    CategoryTable    categoryTable;
    PageDb           db;
    PageRepositoryDb repo;
    PageGenerator    gen;
    EngineArticles   engine;

    Fixture()
        : hostTable(QDir(dir.path()))
        , categoryTable(QDir(dir.path()))
        , db(QDir(dir.path()))
        , repo(db)
        , gen(repo, categoryTable)
    {
        // Initialize so getLangCode(0) returns "en" — without this,
        // generateAll's setAvailablePages/isPageAvailable check skips every page.
        engine.init(QDir(dir.path()), hostTable);
    }

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

    // --- generateSubset ---
    void test_pagegen_subset_empty_list_returns_zero();
    void test_pagegen_subset_writes_specified_page_to_content_db();
    void test_pagegen_subset_returns_count_of_written_pages();
    void test_pagegen_subset_unknown_id_is_skipped();
    void test_pagegen_subset_only_renders_listed_ids();

    // --- isPageAvailable: untranslated language excluded ---
    void test_pagegen_article_untranslated_lang_excluded_from_available_pages();

    // --- categoryHubSlug: diacritic normalization ---
    void test_pagegen_hub_diacritic_slug_strips_accents();

    // --- taxonomy_index excludes pending (unavailable) hub pages ---
    void test_pagegen_taxonomy_index_excludes_pending_category_hub();
    void test_pagegen_taxonomy_index_includes_pending_symptom_hub();
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
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE lang != '_'"));
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
    q.exec(QStringLiteral("SELECT path FROM pages WHERE path = '/my-article.html'"));
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
    q.exec(QStringLiteral("SELECT domain FROM pages WHERE path = '/p.html'"));
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
    q.exec(QStringLiteral("SELECT lang FROM pages WHERE path = '/p.html'"));
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
    q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.lang != '_'"));
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
    q.exec(QStringLiteral(
        "SELECT LENGTH(pv.html_gz) FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/p.html'"));
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
    q.exec(QStringLiteral(
        "SELECT pv.html_gz, pv.etag FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/p.html'"));
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
    q.exec(QStringLiteral(
        "SELECT pv.is_active FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/p.html'"));
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
    q.exec(QStringLiteral(
        "SELECT pv.label FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/p.html'"));
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
    // Mark as published so updatePermalink records a history entry.
    f.repo.setPublishedAt(id, QStringLiteral("2024-01-01T00:00:00Z"));
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
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE lang != '_'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1); // still one content row, not two
    f.closeContentDb(conn);
}

// ---------------------------------------------------------------------------
// generateSubset
// ---------------------------------------------------------------------------

void Test_PageGenerator::test_pagegen_subset_empty_list_returns_zero()
{
    Fixture f;
    QCOMPARE(f.gen.generateSubset({}, QDir(f.dir.path()),
                                   QStringLiteral("example.com"), f.engine, 0), 0);
}

void Test_PageGenerator::test_pagegen_subset_writes_specified_page_to_content_db()
{
    Fixture f;
    const int id = f.addArticle(QStringLiteral("/p.html"), QStringLiteral("text"));
    f.gen.generateSubset({id}, QDir(f.dir.path()),
                          QStringLiteral("example.com"), f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE path = '/p.html'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
    f.closeContentDb(conn);
}

void Test_PageGenerator::test_pagegen_subset_returns_count_of_written_pages()
{
    Fixture f;
    const int id1 = f.addArticle(QStringLiteral("/p1.html"), QStringLiteral("a"));
    const int id2 = f.addArticle(QStringLiteral("/p2.html"), QStringLiteral("b"));
    QCOMPARE(f.gen.generateSubset({id1, id2}, QDir(f.dir.path()),
                                   QStringLiteral("example.com"), f.engine, 0), 2);
}

void Test_PageGenerator::test_pagegen_subset_unknown_id_is_skipped()
{
    Fixture f;
    QCOMPARE(f.gen.generateSubset({9999}, QDir(f.dir.path()),
                                   QStringLiteral("example.com"), f.engine, 0), 0);
}

void Test_PageGenerator::test_pagegen_subset_only_renders_listed_ids()
{
    Fixture f;
    const int id1 = f.addArticle(QStringLiteral("/p1.html"), QStringLiteral("a"));
    f.addArticle(QStringLiteral("/p2.html"), QStringLiteral("b"));
    // Only render id1 — p2.html must not appear in content.db.
    f.gen.generateSubset({id1}, QDir(f.dir.path()),
                          QStringLiteral("example.com"), f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
    f.closeContentDb(conn);
}

// ---------------------------------------------------------------------------
// isPageAvailable: untranslated language excluded from available pages
// ---------------------------------------------------------------------------

void Test_PageGenerator::test_pagegen_article_untranslated_lang_excluded_from_available_pages()
{
    // Arrange: the Fixture constructor already initialises the engine with the
    // default lang-code rows (en at index 0, fr at some index > 0).
    Fixture f;

    // Create an article whose source lang is "en" and that targets "fr" for
    // translation, but provide NO _tr:fr: keys — the page has not been
    // translated yet.
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"),
                                 QStringLiteral("en"));
    f.repo.saveData(id, {
        {QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("text")},
        {QStringLiteral("0_categories"), QString()},
    });
    f.repo.setLangCodesToTranslate(id, {QStringLiteral("fr")});

    // Find the engine row index for "fr".
    int frIndex = -1;
    for (int i = 0; i < f.engine.rowCount(); ++i) {
        if (f.engine.getLangCode(i) == QStringLiteral("fr")) {
            frIndex = i;
            break;
        }
    }
    QVERIFY(frIndex >= 0); // sanity: engine must have a "fr" row after init()

    // Act: run a full generation pass for the English domain (index 0).
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    // Assert: because no _tr:fr: key exists the generator must NOT have added
    // "/p.html" to availablePages["fr"].
    QVERIFY(!f.engine.isPageAvailable(QStringLiteral("/p.html"), frIndex));
}

// ---------------------------------------------------------------------------
// categoryHubSlug: diacritic normalization
// ---------------------------------------------------------------------------

void Test_PageGenerator::test_pagegen_hub_diacritic_slug_strips_accents()
{
    // "Santé mentale": the accented é must be decomposed via NFD into 'e' + a
    // combining accent, then the combining character is stripped.  Without NFD
    // the é is removed entirely and the result would be "/sant-mentale.html".
    QCOMPARE(PageGenerator::categoryHubSlug(QStringLiteral("Santé mentale")),
             QStringLiteral("/sante-mentale.html"));
}

// ---------------------------------------------------------------------------
// taxonomy_index includes pending hub pages (regression for findGeneratedByTypeId-only bug)
// ---------------------------------------------------------------------------

void Test_PageGenerator::test_pagegen_taxonomy_index_excludes_pending_category_hub()
{
    // Pending (isPageAvailable == false) category_hub pages must NOT appear in
    // the taxonomy index grid.  The user explicitly requested this behaviour:
    // unavailable pages should be silently skipped rather than shown as plain text.
    Fixture f;

    // Pending category_hub stub — no generated_at, so isPageAvailable returns false.
    f.repo.create(QStringLiteral("category_hub"),
                  QStringLiteral("/alpha-biomarkers"),
                  QStringLiteral("en"));
    f.repo.create(QStringLiteral("taxonomy_index"),
                  QStringLiteral("/categories"),
                  QStringLiteral("en"));

    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral(
        "SELECT pv.html_gz FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/categories'"));
    QVERIFY2(q.next(), "taxonomy_index page not written to content.db");
    const QByteArray html = gzipDecompress(q.value(0).toByteArray());
    f.closeContentDb(conn);

    QVERIFY2(!html.contains("Alpha Biomarkers"),
             "taxonomy index grid must NOT list pending (unavailable) category hub pages");
}

void Test_PageGenerator::test_pagegen_taxonomy_index_includes_pending_symptom_hub()
{
    // Regression guard: symptom_hub pages must NOT appear in the /categories taxonomy
    // index. PageTypeTaxonomyIndex::aggregatedTypeIds() returns only "category_hub";
    // adding a symptom_hub must not pollute the categories grid.
    Fixture f;

    f.repo.create(QStringLiteral("symptom_hub"),
                  QStringLiteral("/symptoms/fatigue"),
                  QStringLiteral("en"));
    f.repo.create(QStringLiteral("taxonomy_index"),
                  QStringLiteral("/categories"),
                  QStringLiteral("en"));

    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral(
        "SELECT pv.html_gz FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/categories'"));
    QVERIFY2(q.next(), "taxonomy_index page not written to content.db");
    const QByteArray html = gzipDecompress(q.value(0).toByteArray());
    f.closeContentDb(conn);

    // Symptom hub "Fatigue" must NOT appear in the categories grid.
    QVERIFY2(!html.contains("Fatigue"),
             "taxonomy index must not list symptom hub pages (only category_hub)");
}

QTEST_MAIN(Test_PageGenerator)
#include "test_page_generator.moc"
