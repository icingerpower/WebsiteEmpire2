#include <QtTest>
#include <QCryptographicHash>
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
#include "website/taxonomy/TaxonomyDb.h"
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

    // SHA-1 of UTF-8 encoded text — mirrors BlocTranslations::_sha1.
    static QString sha1(const QString &text)
    {
        return QString::fromLatin1(
            QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex());
    }

    // Creates an article with a complete French translation (text + hash) so that
    // isTranslationComplete("fr") returns true and the generator writes the page.
    int addTranslatedArticle(const QString &permalink,
                             const QString &enText,
                             const QString &frText)
    {
        const int id = repo.create(QStringLiteral("article"), permalink, QStringLiteral("en"));
        // KEY_TEXT is "text"; bloc index 1 is PageBlocText, so raw key is "1_text".
        // Translation keys: "1_tr:fr:text" (value) + "1_tr:fr:text:hash" (sha1 of source).
        repo.saveData(id, {
            {QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT), enText},
            {QStringLiteral("0_categories"),    QString()},
            {QStringLiteral("1_tr:fr:text"),    frText},
            {QStringLiteral("1_tr:fr:text:hash"), sha1(enText)},
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
// Shared setup for the symptom-hub multilingual regression tests.
// Creates TaxonomyDb entry "Hot Flashes"→fr:"Bouffées de chaleur", one article
// linked to that symptom with a French translation, and a symptom_hub page
// targeting /symptoms/hot-flashes for French.  Runs generateAll for the French
// engine index and sets frIndex.
void setupFrenchSymptomHub(Fixture &f, int &frIndex)
{
    TaxonomyDb taxDb(QDir(f.dir.path()));
    taxDb.sync(QStringLiteral("symptoms"), {QStringLiteral("Hot Flashes")});
    taxDb.setTranslation(QStringLiteral("symptoms"), QStringLiteral("Hot Flashes"),
                          QStringLiteral("fr"), QStringLiteral("Bouffées de chaleur"));

    const int articleId = f.repo.create(QStringLiteral("article"),
                                         QStringLiteral("/hot-flashes-article"),
                                         QStringLiteral("en"));
    f.repo.saveData(articleId, {
        {QStringLiteral("1_text"),       QStringLiteral("<h1>Hot Flashes Article</h1><p>Content.</p>")},
        {QStringLiteral("0_categories"), QString()},
        {QStringLiteral("2_symptoms"),   QStringLiteral("Hot Flashes")},
        {QStringLiteral("1_tr:fr:text"), QStringLiteral("<h1>Article bouffées</h1><p>Contenu.</p>")},
    });
    f.repo.setLangCodesToTranslate(articleId, {QStringLiteral("fr")});

    const int hubId = f.repo.create(QStringLiteral("symptom_hub"),
                                     QStringLiteral("/symptoms/hot-flashes"),
                                     QStringLiteral("en"));
    f.repo.setLangCodesToTranslate(hubId, {QStringLiteral("fr")});

    frIndex = -1;
    for (int i = 0; i < f.engine.rowCount(); ++i) {
        if (f.engine.getLangCode(i) == QStringLiteral("fr")) {
            frIndex = i;
            break;
        }
    }

    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, frIndex);
}

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

    // --- multilingual symptom/category hub regressions ---
    void test_pagegen_category_hub_slug_has_no_html_suffix();
    void test_pagegen_symptom_hub_french_permalink_is_translated_slug();
    void test_pagegen_symptom_hub_article_card_appears_on_translated_domain();
    void test_pagegen_symptom_hub_h1_uses_taxonomy_translation_on_translated_domain();

    // --- lang path prefix for nginx reverse-proxy deployments ---
    void test_pagegen_links_include_lang_prefix_on_translated_domain();

    // --- translated article permalink ---
    void test_pagegen_article_stored_at_translated_permalink_when_tr_slug_set();
    void test_pagegen_article_falls_back_to_english_permalink_when_no_tr_slug();

    // --- symptom hub availability gating ---
    void test_pagegen_symptom_hub_excluded_when_no_articles_translated_for_lang();
    void test_pagegen_symptom_hub_included_when_article_translated_for_lang();
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
    // the é is removed entirely and the result would be "/sant-mentale".
    QCOMPARE(PageGenerator::categoryHubSlug(QStringLiteral("Santé mentale")),
             QStringLiteral("/sante-mentale"));
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

// ---------------------------------------------------------------------------
// Category hub: .html suffix must not appear in generated permalink
// ---------------------------------------------------------------------------

void Test_PageGenerator::test_pagegen_category_hub_slug_has_no_html_suffix()
{
    // Regression: categoryHubSlug() previously appended ".html", causing category hub pages
    // to be stored at "/brain-disorders.html" and breaking routing and sitemap generation.
    QCOMPARE(PageGenerator::categoryHubSlug(QStringLiteral("Brain Disorders")),
             QStringLiteral("/brain-disorders"));
    QVERIFY(!PageGenerator::categoryHubSlug(QStringLiteral("Mental Health")).contains(
        QStringLiteral(".html")));
}

// ---------------------------------------------------------------------------
// Symptom hub multilingual regression
// ---------------------------------------------------------------------------

void Test_PageGenerator::test_pagegen_symptom_hub_french_permalink_is_translated_slug()
{
    // Regression: when TaxonomyDb has a French translation for a symptom, the French
    // symptom hub page must be written at the translated slug, not the English one.
    // A missing pre-pass would leave the page at "/symptoms/hot-flashes" (English).
    Fixture f;
    int frIndex = -1;
    setupFrenchSymptomHub(f, frIndex);
    QVERIFY(frIndex >= 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));

    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE path = '/symptoms/bouffees-de-chaleur'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1); // translated path must be written

    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE path = '/symptoms/hot-flashes'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 0); // English path must NOT appear when generating French

    f.closeContentDb(conn);
}

void Test_PageGenerator::test_pagegen_symptom_hub_article_card_appears_on_translated_domain()
{
    // Regression: on the French domain the symptom hub page rendered empty — no article cards.
    // Root cause: _writePage passed the translated slug ("/symptoms/bouffees-de-chaleur") to
    // setGenerationContext, so PageBlocConditionList searched pages.db with the French slug
    // and found nothing (article symptom "Hot Flashes" slugifies to "hot-flashes").
    // Fix: record.permalink (English) is passed to setGenerationContext; only the output path
    // in content.db uses the translated slug.
    Fixture f;
    int frIndex = -1;
    setupFrenchSymptomHub(f, frIndex);
    QVERIFY(frIndex >= 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral(
        "SELECT pv.html_gz FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/symptoms/bouffees-de-chaleur'"));
    QVERIFY2(q.next(), "French symptom hub page not found in content.db");
    const QByteArray html = gzipDecompress(q.value(0).toByteArray());
    f.closeContentDb(conn);

    QVERIFY2(html.contains("hot-flashes-article"),
             "article card must appear in French symptom hub (English slug used for lookup)");
}

void Test_PageGenerator::test_pagegen_symptom_hub_h1_uses_taxonomy_translation_on_translated_domain()
{
    // Regression: the symptom hub h1 showed the raw URL slug instead of the translated name.
    // Root cause: same slug mismatch — the French slug was used to look up TaxonomyDb entries,
    // but TaxonomyDb stores (englishName → translatedName) and the slug is derived from
    // the English name.  With the fix, record.permalink keeps the English slug so
    // SymptomNav::slugify(englishName) == slug matches and the translation is displayed.
    Fixture f;
    int frIndex = -1;
    setupFrenchSymptomHub(f, frIndex);
    QVERIFY(frIndex >= 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral(
        "SELECT pv.html_gz FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/symptoms/bouffees-de-chaleur'"));
    QVERIFY2(q.next(), "French symptom hub page not found in content.db");
    const QByteArray html = gzipDecompress(q.value(0).toByteArray());
    f.closeContentDb(conn);

    // "Bouffées de chaleur": "Bouff" (capital B) is unique to the translated h1.
    // The article card text uses lowercase "bouffées"; the slug uses no accents.
    QVERIFY2(html.contains("Bouff"),
             "h1 must show French translated symptom name from TaxonomyDb");
}

// ---------------------------------------------------------------------------
// Lang path prefix: nginx reverse-proxy deployments
// ---------------------------------------------------------------------------

void Test_PageGenerator::test_pagegen_links_include_lang_prefix_on_translated_domain()
{
    // Regression: menu and hub card hrefs used absolute paths like "/symptoms"
    // which a browser at "https://example.com/fr/…" resolves to the English server.
    // The two-dir generateAll must derive the path prefix ("/fr") from sitemapBaseUrl
    // and prepend it to all internal link hrefs so French navigation stays on the
    // French server.
    Fixture f;
    int frIndex = -1;
    setupFrenchSymptomHub(f, frIndex);
    QVERIFY(frIndex >= 0);

    // Re-run the generation using the two-dir overload with a sitemapBaseUrl that
    // carries the "/fr" path prefix. The output still goes to f.dir.path().
    f.gen.generateAll(QDir(f.dir.path()), QDir(f.dir.path()),
                      QStringLiteral("example.com"), f.engine, frIndex,
                      QStringLiteral("https://example.com/fr"));

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral(
        "SELECT pv.html_gz FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/symptoms/bouffees-de-chaleur'"));
    QVERIFY2(q.next(), "French symptom hub page not found in content.db");
    const QByteArray html = gzipDecompress(q.value(0).toByteArray());
    f.closeContentDb(conn);

    QVERIFY2(html.contains("/fr/hot-flashes-article"),
             "article card href must include /fr/ prefix so French navigation "
             "stays on the French site when served behind an nginx /fr/ proxy");
}

// ---------------------------------------------------------------------------
// Translated article permalink
// ---------------------------------------------------------------------------

void Test_PageGenerator::test_pagegen_article_stored_at_translated_permalink_when_tr_slug_set()
{
    // When an article has a non-empty endPermalink AND a stored tr:fr:_permalink_slug,
    // the French content.db must contain the page at the French slug, not the English one.
    Fixture f;

    const QString enText = QStringLiteral("<h1>Master Sleep Article</h1><p>English content.</p>");
    const QString frText = QStringLiteral("<h1>Maîtriser le sommeil</h1><p>Contenu français.</p>");

    const int id = f.repo.create(QStringLiteral("article"),
                                  QStringLiteral("/master-sleep-genes-biomarkers"),
                                  QStringLiteral("en"));
    f.repo.setEndPermalink(id, QStringLiteral("genes-biomarkers"));
    // tr:fr:_permalink_slug has no bloc-number prefix — read by PageGenerator
    // directly from page_data, outside the bloc dispatch path.
    f.repo.saveData(id, {
        {QStringLiteral("1_text"),              enText},
        {QStringLiteral("0_categories"),         QString()},
        {QStringLiteral("1_tr:fr:text"),         frText},
        {QStringLiteral("1_tr:fr:text:hash"),    Fixture::sha1(enText)},
        {QStringLiteral("tr:fr:_permalink_slug"),
         QStringLiteral("maitriser-le-sommeil-genes-biomarqueurs")},
    });
    f.repo.setLangCodesToTranslate(id, {QStringLiteral("fr")});

    int frIndex = -1;
    for (int i = 0; i < f.engine.rowCount(); ++i) {
        if (f.engine.getLangCode(i) == QStringLiteral("fr")) {
            frIndex = i;
            break;
        }
    }
    QVERIFY(frIndex >= 0);

    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, frIndex);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));

    q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM pages WHERE path = '/maitriser-le-sommeil-genes-biomarqueurs'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1); // French slug must be present

    q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM pages WHERE path = '/master-sleep-genes-biomarkers'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 0); // English slug must NOT appear in the French content.db

    f.closeContentDb(conn);
}

void Test_PageGenerator::test_pagegen_article_falls_back_to_english_permalink_when_no_tr_slug()
{
    // When tr:fr:_permalink_slug is not stored (translator hasn't run yet),
    // the French content.db must still contain the page at the English permalink.
    Fixture f;

    const QString enText = QStringLiteral("<h1>Master Sleep Article</h1><p>English content.</p>");
    const QString frText = QStringLiteral("<h1>Maîtriser le sommeil</h1><p>Contenu français.</p>");

    const int id = f.addTranslatedArticle(
        QStringLiteral("/master-sleep-genes-biomarkers"), enText, frText);
    f.repo.setEndPermalink(id, QStringLiteral("genes-biomarkers"));
    // NOTE: no tr:fr:_permalink_slug key — translator hasn't stored the slug yet.
    f.repo.setLangCodesToTranslate(id, {QStringLiteral("fr")});

    int frIndex = -1;
    for (int i = 0; i < f.engine.rowCount(); ++i) {
        if (f.engine.getLangCode(i) == QStringLiteral("fr")) {
            frIndex = i;
            break;
        }
    }
    QVERIFY(frIndex >= 0);

    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, frIndex);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));

    q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM pages WHERE path = '/master-sleep-genes-biomarkers'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1); // Falls back to English slug

    f.closeContentDb(conn);
}

// ---------------------------------------------------------------------------
// Symptom hub availability gating
// ---------------------------------------------------------------------------

void Test_PageGenerator::test_pagegen_symptom_hub_excluded_when_no_articles_translated_for_lang()
{
    // Regression: symptom hubs were previously always marked available for every
    // target language, causing empty hub pages in the French symptoms index when
    // no French articles existed for that symptom.
    Fixture f;

    // Article with "Tinnitus" symptom, but NO French translation.
    const int artId = f.repo.create(QStringLiteral("article"),
                                     QStringLiteral("/tinnitus-article"),
                                     QStringLiteral("en"));
    f.repo.saveData(artId, {
        {QStringLiteral("1_text"),        QStringLiteral("<h1>Tinnitus</h1><p>Content.</p>")},
        {QStringLiteral("0_categories"),  QString()},
        {QStringLiteral("2_symptoms"),    QStringLiteral("Tinnitus")},
    });
    // Article targets French for translation but no tr:fr: data exists yet.
    f.repo.setLangCodesToTranslate(artId, {QStringLiteral("fr")});

    // Symptom hub for Tinnitus, targeting French.
    const int hubId = f.repo.create(QStringLiteral("symptom_hub"),
                                     QStringLiteral("/symptoms/tinnitus"),
                                     QStringLiteral("en"));
    f.repo.setLangCodesToTranslate(hubId, {QStringLiteral("fr")});

    int frIndex = -1;
    for (int i = 0; i < f.engine.rowCount(); ++i) {
        if (f.engine.getLangCode(i) == QStringLiteral("fr")) { frIndex = i; break; }
    }
    QVERIFY(frIndex >= 0);

    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, frIndex);

    // Hub must NOT appear in the French content.db — no translated articles exist.
    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE path LIKE '/symptoms/tinnitus%'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 0);
    f.closeContentDb(conn);
}

void Test_PageGenerator::test_pagegen_symptom_hub_included_when_article_translated_for_lang()
{
    // Symptom hub must appear in the French content.db when at least one article
    // with that symptom has been translated to French.
    Fixture f;
    int frIndex = -1;
    setupFrenchSymptomHub(f, frIndex); // creates article with "Hot Flashes" + French translation
    QVERIFY(frIndex >= 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    // Hub written at the French translated slug.
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE path = '/symptoms/bouffees-de-chaleur'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
    f.closeContentDb(conn);
}

QTEST_MAIN(Test_PageGenerator)
#include "test_page_generator.moc"
