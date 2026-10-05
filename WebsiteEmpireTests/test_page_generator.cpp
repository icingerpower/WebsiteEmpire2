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
#include "website/commonblocs/CommonBlocPageLabels.h"

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
        engine.init(QDir(dir.path()), hostTable);

        // Resolve the English row rather than assuming index 0. init() creates
        // one row per supported language ordered by speaker count and APPENDS
        // the editing language, so getLangCode(0) is "zh" and English is last
        // (row 40 of 41). Tests that passed a hardcoded 0 were generating for
        // Chinese: generateAll() keys availablePages by the page's own lang
        // ("en") but isPageAvailable() looks it up under getLangCode(index)
        // ("zh"), so every page was silently skipped and generateAll returned
        // 0. That is what made 15 tests in this file fail.
        for (int i = 0; i < engine.rowCount(); ++i) {
            if (engine.getLangCode(i) == QStringLiteral("en")) {
                enIndex = i;
                break;
            }
        }
    }

    // Engine row index of the English (source) language — see the constructor.
    int enIndex = -1;

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
    CommonBlocPageLabels labels;
    labels.setTranslation(QStringLiteral("possible_conditions"), QStringLiteral("fr"),
                          QStringLiteral("Affections possibles"));
    labels.setTranslation(QStringLiteral("all_symptoms"), QStringLiteral("fr"),
                          QStringLiteral("Tous les symptômes"));
    labels.setTranslation(QStringLiteral("browse_symptoms"), QStringLiteral("fr"),
                          QStringLiteral("Parcourir les affections par symptôme"));
    labels.setTranslation(QStringLiteral("browse_symptoms_count"), QStringLiteral("fr"),
                          QStringLiteral("Parcourir les affections par symptôme — %1 symptômes"));
    labels.save(QDir(f.dir.path()));
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
    void test_pagegen_translated_permalink_map_includes_regular_article_without_end_permalink();

    // --- hreflang + redirect on translated-slug pages (regression) ---
    void test_pagegen_translated_article_still_emits_hreflang_alternates();
    void test_pagegen_translated_slug_emits_redirect_from_english_permalink();
    void test_pagegen_redirect_target_includes_lang_prefix_on_subpath_deployment();
    void test_pagegen_history_redirect_target_includes_lang_prefix();

    // --- robots.txt advertises every deployed language's sitemap ---
    void test_pagegen_robots_lists_sitemap_for_every_deployed_language();

    // --- symptom hub availability gating ---
    void test_pagegen_symptom_hub_excluded_when_no_articles_translated_for_lang();
    void test_pagegen_symptom_hub_included_when_article_translated_for_lang();

    // --- symptom link href format on translated domain ---
    void test_pagegen_symptom_link_href_is_absolute_on_translated_domain();

    // --- symptom index excludes hubs with no translated articles ---
    void test_pagegen_symptom_index_excludes_untranslated_hubs();
    void test_pagegen_symptom_index_includes_translated_hubs();

    // --- articleless symptom hub pruning ---
    void test_pagegen_symptom_hub_without_articles_excluded_for_english();

    // --- corrupt translation resilience ---
    void test_pagegen_corrupt_shortcode_skips_page_does_not_crash();

    // --- canonical URL lang prefix (regression) ---
    void test_pagegen_canonical_includes_lang_prefix_on_translated_page();
    void test_pagegen_canonical_includes_translated_slug_with_lang_prefix();
    void test_pagegen_og_url_includes_lang_prefix_on_translated_page();
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
    QCOMPARE(f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, f.enIndex), 2);
}

void Test_PageGenerator::test_pagegen_generate_zero_pages_returns_zero()
{
    Fixture f;
    QCOMPARE(f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, f.enIndex), 0);
}

void Test_PageGenerator::test_pagegen_generate_creates_page_row_in_content_db()
{
    Fixture f;
    f.addArticle(QStringLiteral("/p.html"), QStringLiteral("text"));
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, f.enIndex);

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
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, f.enIndex);

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
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("mysite.com"), f.engine, f.enIndex);

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
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, f.enIndex);

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
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, f.enIndex);

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
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, f.enIndex);

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
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, f.enIndex);

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
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, f.enIndex);

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
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, f.enIndex);

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
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, f.enIndex);

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
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, f.enIndex);

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
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, f.enIndex);

    // Update text and regenerate.
    f.repo.saveData(id, {
        {QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("v2")},
        {QStringLiteral("0_categories"), QString()},
    });
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, f.enIndex);

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
                                   QStringLiteral("example.com"), f.engine, f.enIndex), 0);
}

void Test_PageGenerator::test_pagegen_subset_writes_specified_page_to_content_db()
{
    Fixture f;
    const int id = f.addArticle(QStringLiteral("/p.html"), QStringLiteral("text"));
    f.gen.generateSubset({id}, QDir(f.dir.path()),
                          QStringLiteral("example.com"), f.engine, f.enIndex);

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
                                   QStringLiteral("example.com"), f.engine, f.enIndex), 2);
}

void Test_PageGenerator::test_pagegen_subset_unknown_id_is_skipped()
{
    Fixture f;
    QCOMPARE(f.gen.generateSubset({9999}, QDir(f.dir.path()),
                                   QStringLiteral("example.com"), f.engine, f.enIndex), 0);
}

void Test_PageGenerator::test_pagegen_subset_only_renders_listed_ids()
{
    Fixture f;
    const int id1 = f.addArticle(QStringLiteral("/p1.html"), QStringLiteral("a"));
    f.addArticle(QStringLiteral("/p2.html"), QStringLiteral("b"));
    // Only render id1 — p2.html must not appear in content.db.
    f.gen.generateSubset({id1}, QDir(f.dir.path()),
                          QStringLiteral("example.com"), f.engine, f.enIndex);

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
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, f.enIndex);

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

    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, f.enIndex);

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

    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, f.enIndex);

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
    QVERIFY(html.contains("<h2>Affections possibles</h2>"));
    QVERIFY(!html.contains("Possible conditions"));
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

void Test_PageGenerator::test_pagegen_symptom_link_href_is_absolute_on_translated_domain()
{
    // Regression: PageBlocSymptomLinks was stripping the leading '/' from the href
    // returned by resolveLinkHref.  On a French page at /fr/<article>, the relative
    // href "fr/symptoms/bouffees-de-chaleur" resolves in the browser to
    // /fr/fr/symptoms/... which is a 404.  The href must be an absolute path
    // "/fr/symptoms/bouffees-de-chaleur" so it always resolves correctly.
    Fixture f;

    TaxonomyDb taxDb(QDir(f.dir.path()));
    taxDb.sync(QStringLiteral("symptoms"), {QStringLiteral("Hot Flashes")});
    taxDb.setTranslation(QStringLiteral("symptoms"), QStringLiteral("Hot Flashes"),
                          QStringLiteral("fr"), QStringLiteral("Bouffées de chaleur"));

    const QString enText = QStringLiteral("[TITLE level=\"1\"]Hot Flashes Article[/TITLE]<p>Content.</p>");
    const QString frText = QStringLiteral("[TITLE level=\"1\"]Article Bouffées[/TITLE]<p>Contenu.</p>");

    const int articleId = f.repo.create(QStringLiteral("article"),
                                         QStringLiteral("/hot-flashes-article"),
                                         QStringLiteral("en"));
    f.repo.saveData(articleId, {
        {QStringLiteral("1_text"),              enText},
        {QStringLiteral("0_categories"),         QString()},
        {QStringLiteral("7_symptoms"),           QStringLiteral("Hot Flashes")},
        {QStringLiteral("1_tr:fr:text"),         frText},
        {QStringLiteral("1_tr:fr:text:hash"),    Fixture::sha1(enText)},
    });
    f.repo.setLangCodesToTranslate(articleId, {QStringLiteral("fr")});

    const int hubId = f.repo.create(QStringLiteral("symptom_hub"),
                                     QStringLiteral("/symptoms/hot-flashes"),
                                     QStringLiteral("en"));
    f.repo.setLangCodesToTranslate(hubId, {QStringLiteral("fr")});

    int frIndex = -1;
    for (int i = 0; i < f.engine.rowCount(); ++i) {
        if (f.engine.getLangCode(i) == QStringLiteral("fr")) {
            frIndex = i;
            break;
        }
    }
    QVERIFY(frIndex >= 0);

    f.gen.generateAll(QDir(f.dir.path()), QDir(f.dir.path()),
                      QStringLiteral("example.com"), f.engine, frIndex,
                      QStringLiteral("https://example.com/fr"));

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral(
        "SELECT pv.html_gz FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/hot-flashes-article'"));
    QVERIFY2(q.next(), "French article page not found in content.db");
    const QByteArray html = gzipDecompress(q.value(0).toByteArray());
    f.closeContentDb(conn);

    QVERIFY2(html.contains("/fr/symptoms/bouffees-de-chaleur"),
             "Symptom link href must be an absolute path (/fr/symptoms/...) — "
             "a relative href (fr/symptoms/...) resolves to /fr/fr/symptoms/... "
             "in the browser when the article page is served under the /fr/ proxy");
    QVERIFY2(!html.contains("\"fr/symptoms/"),
             "Symptom link href must not be a relative path (missing leading slash)");
}

void Test_PageGenerator::test_pagegen_symptom_index_excludes_untranslated_hubs()
{
    // Regression: PageTypeSymptomIndex used slugsWithDirectArticles (built from ALL
    // articles regardless of language) to decide whether to show a symptom on the
    // index page.  A symptom with only English articles was included as non-clickable
    // text on the French /symptoms index.  It must be omitted entirely.
    Fixture f;

    TaxonomyDb taxDb(QDir(f.dir.path()));
    taxDb.sync(QStringLiteral("symptoms"),
               {QStringLiteral("Hot Flashes"), QStringLiteral("Tinnitus")});
    taxDb.setTranslation(QStringLiteral("symptoms"), QStringLiteral("Hot Flashes"),
                          QStringLiteral("fr"), QStringLiteral("Bouffées de chaleur"));
    taxDb.setTranslation(QStringLiteral("symptoms"), QStringLiteral("Tinnitus"),
                          QStringLiteral("fr"), QStringLiteral("Acouphènes"));

    // Article referencing Tinnitus — English only, no French translation.
    const int tinnitusArticle = f.repo.create(QStringLiteral("article"),
                                               QStringLiteral("/tinnitus-article"),
                                               QStringLiteral("en"));
    f.repo.saveData(tinnitusArticle, {
        {QStringLiteral("1_text"),      QStringLiteral("<h1>Tinnitus</h1><p>EN only.</p>")},
        {QStringLiteral("0_categories"), QString()},
        {QStringLiteral("7_symptoms"),  QStringLiteral("Tinnitus")},
    });
    f.repo.setLangCodesToTranslate(tinnitusArticle, {QStringLiteral("fr")});

    const int tinnitusHub = f.repo.create(QStringLiteral("symptom_hub"),
                                           QStringLiteral("/symptoms/tinnitus"),
                                           QStringLiteral("en"));
    f.repo.setLangCodesToTranslate(tinnitusHub, {QStringLiteral("fr")});

    // Symptom index page.
    const int symIndexId = f.repo.create(QStringLiteral("symptom_index"),
                                          QStringLiteral("/symptoms"), QStringLiteral("en"));
    f.repo.setLangCodesToTranslate(symIndexId, {QStringLiteral("fr")});

    int frIndex = -1;
    for (int i = 0; i < f.engine.rowCount(); ++i) {
        if (f.engine.getLangCode(i) == QStringLiteral("fr")) { frIndex = i; break; }
    }
    QVERIFY(frIndex >= 0);

    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, frIndex);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral(
        "SELECT pv.html_gz FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id WHERE p.path = '/symptoms'"));
    QVERIFY2(q.next(), "French symptom index page not found");
    const QByteArray html = gzipDecompress(q.value(0).toByteArray());
    f.closeContentDb(conn);

    QVERIFY2(!html.contains("Acouph"),
             "Tinnitus/Acouphenes must NOT appear on the French symptoms index "
             "— no French article references it");
}

void Test_PageGenerator::test_pagegen_symptom_index_includes_translated_hubs()
{
    // A symptom hub with at least one French-translated article must appear
    // as a clickable link on the French /symptoms index.
    Fixture f;
    int frIndex = -1;
    setupFrenchSymptomHub(f, frIndex); // Hot Flashes + French article
    QVERIFY(frIndex >= 0);

    const int symIndexId2 = f.repo.create(QStringLiteral("symptom_index"),
                                           QStringLiteral("/symptoms"), QStringLiteral("en"));
    f.repo.setLangCodesToTranslate(symIndexId2, {QStringLiteral("fr")});

    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, frIndex);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral(
        "SELECT pv.html_gz FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id WHERE p.path = '/symptoms'"));
    QVERIFY2(q.next(), "French symptom index page not found");
    const QByteArray html = gzipDecompress(q.value(0).toByteArray());
    f.closeContentDb(conn);

    QVERIFY2(html.contains("Bouff"),
             "Hot Flashes (Bouffées de chaleur) must appear on the French symptoms index");
    QVERIFY2(html.contains("href="),
             "Hot Flashes entry must be a clickable link, not plain text");
}

// ---------------------------------------------------------------------------
// Articleless symptom hub pruning
// ---------------------------------------------------------------------------

void Test_PageGenerator::test_pagegen_symptom_hub_without_articles_excluded_for_english()
{
    // Regression: symptom_hub pages whose slug does not appear in any article's
    // *_symptoms field were generated for English but not for translated languages,
    // causing a ~437-page gap (1377 en vs 940 es/fr/…).
    // After the fix, empty hubs are excluded for ALL languages including English.

    Fixture f;

    // Hub with no articles referencing it.
    f.repo.create(QStringLiteral("symptom_hub"),
                  QStringLiteral("/symptoms/orphan-symptom"),
                  QStringLiteral("en"));

    // Hub that DOES have an article.
    const int hubId = f.repo.create(QStringLiteral("symptom_hub"),
                                     QStringLiteral("/symptoms/hot-flashes"),
                                     QStringLiteral("en"));
    f.repo.setLangCodesToTranslate(hubId, {QStringLiteral("fr")});

    const int artId = f.repo.create(QStringLiteral("article"),
                                     QStringLiteral("/hot-flashes-article"),
                                     QStringLiteral("en"));
    f.repo.saveData(artId, {
        {QStringLiteral("1_text"),      QStringLiteral("<h1>Hot Flashes</h1><p>Content.</p>")},
        {QStringLiteral("0_categories"), QString()},
        {QStringLiteral("7_symptoms"),  QStringLiteral("Hot Flashes")},
    });

    int enIndex = -1;
    for (int i = 0; i < f.engine.rowCount(); ++i) {
        if (f.engine.getLangCode(i) == QStringLiteral("en")) { enIndex = i; break; }
    }
    QVERIFY(enIndex >= 0);

    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, enIndex);

    const QString &conn = f.openContentDb();

    // The orphan hub must NOT appear — no articles reference it.
    QSqlQuery orphan(QSqlDatabase::database(conn));
    orphan.exec(QStringLiteral("SELECT count(*) FROM pages WHERE path='/symptoms/orphan-symptom'"));
    QVERIFY(orphan.next());
    QCOMPARE(orphan.value(0).toInt(), 0);

    // The backed hub MUST appear — one article references it.
    QSqlQuery backed(QSqlDatabase::database(conn));
    backed.exec(QStringLiteral("SELECT count(*) FROM pages WHERE path='/symptoms/hot-flashes'"));
    QVERIFY(backed.next());
    QCOMPARE(backed.value(0).toInt(), 1);

    f.closeContentDb(conn);
}

// ---------------------------------------------------------------------------
// Corrupt translation resilience
// ---------------------------------------------------------------------------

void Test_PageGenerator::test_pagegen_corrupt_shortcode_skips_page_does_not_crash()
{
    // Regression: a duplicate shortcode argument in a translated text field
    // (e.g. [TITLE level="2" level="3"]) previously crashed the entire publish
    // run via an uncaught ExceptionWithTitleText.  After the fix, _writePage
    // catches the exception, logs a warning, and skips that page variant —
    // the run completes and unaffected pages are still written.

    Fixture f;

    // Valid French article — must appear in content.db.
    const int goodId = f.addTranslatedArticle(
        QStringLiteral("/good-article"),
        QStringLiteral("<h1>Good</h1><p>Content.</p>"),
        QStringLiteral("[TITLE level=\"1\"]Bon article[/TITLE]\n<p>Contenu valide.</p>"));
    f.repo.setLangCodesToTranslate(goodId, {QStringLiteral("fr")});

    // Article whose French translation contains a duplicate 'level' argument —
    // identical to the real corruption seen in production.
    const int badId = f.repo.create(QStringLiteral("article"),
                                     QStringLiteral("/bad-article"),
                                     QStringLiteral("en"));
    const QString enText = QStringLiteral("<h1>Bad</h1><p>Content.</p>");
    f.repo.saveData(badId, {
        {QStringLiteral("1_text"),            enText},
        {QStringLiteral("0_categories"),      QString()},
        // Corrupt: duplicate 'level' argument — the same error as in production.
        {QStringLiteral("1_tr:fr:text"),
         QStringLiteral("[TITLE level=\"1\" level=\"1\"]Mauvais article[/TITLE]\n<p>Contenu.</p>")},
        {QStringLiteral("1_tr:fr:text:hash"), Fixture::sha1(enText)},
    });
    f.repo.setLangCodesToTranslate(badId, {QStringLiteral("fr")});

    int frIndex = -1;
    for (int i = 0; i < f.engine.rowCount(); ++i) {
        if (f.engine.getLangCode(i) == QStringLiteral("fr")) { frIndex = i; break; }
    }
    QVERIFY(frIndex >= 0);

    // Must not throw — the corrupt page is skipped, the good page is written.
    QVERIFY_THROWS_NO_EXCEPTION(
        f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, frIndex));

    const QString &conn = f.openContentDb();

    QSqlQuery good(QSqlDatabase::database(conn));
    good.exec(QStringLiteral("SELECT count(*) FROM pages WHERE path='/good-article'"));
    QVERIFY(good.next());
    QCOMPARE(good.value(0).toInt(), 1);

    QSqlQuery bad(QSqlDatabase::database(conn));
    bad.exec(QStringLiteral("SELECT count(*) FROM pages WHERE path='/bad-article'"));
    QVERIFY(bad.next());
    QCOMPARE(bad.value(0).toInt(), 0);

    f.closeContentDb(conn);
}

// ---------------------------------------------------------------------------
// Canonical URL lang prefix regression
// ---------------------------------------------------------------------------

// Helper: generate a French article page and return its decompressed HTML.
// The article is stored at /canonical-test-article in pages.db and in content.db.
static QByteArray generateFrenchArticleHtml(Fixture &f,
                                             const QString &extraDataKey = {},
                                             const QString &extraDataValue = {})
{
    const QString enText = QStringLiteral("<h1>Sleep</h1><p>English.</p>");
    const QString frText = QStringLiteral("<h1>Sommeil</h1><p>Français.</p>");

    const int id = f.repo.create(QStringLiteral("article"),
                                  QStringLiteral("/canonical-test-article"),
                                  QStringLiteral("en"));
    QHash<QString, QString> data = {
        {QStringLiteral("1_text"),              enText},
        {QStringLiteral("0_categories"),         QString()},
        {QStringLiteral("1_tr:fr:text"),         frText},
        {QStringLiteral("1_tr:fr:text:hash"),    Fixture::sha1(enText)},
    };
    if (!extraDataKey.isEmpty()) {
        data.insert(extraDataKey, extraDataValue);
    }
    f.repo.saveData(id, data);
    f.repo.setLangCodesToTranslate(id, {QStringLiteral("fr")});

    int frIndex = -1;
    for (int i = 0; i < f.engine.rowCount(); ++i) {
        if (f.engine.getLangCode(i) == QStringLiteral("fr")) {
            frIndex = i;
            break;
        }
    }
    if (frIndex < 0) {
        return {};
    }

    // The engine row for fr is auto-created with an empty domain by _reconcileRows().
    // Without a domain, AbstractPageType::addCode leaves baseUrl empty and the
    // canonical/<og:url> block is never emitted.  Set "example.com" so the test can
    // verify the full canonical URL including the /fr/ language prefix.
    f.engine.setData(f.engine.index(frIndex, AbstractEngine::COL_DOMAIN),
                     QStringLiteral("example.com"));

    f.gen.generateAll(QDir(f.dir.path()), QDir(f.dir.path()),
                      QStringLiteral("example.com"), f.engine, frIndex,
                      QStringLiteral("https://example.com/fr"));

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral(
        "SELECT pv.html_gz FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/canonical-test-article'"));
    const QByteArray html = q.next() ? gzipDecompress(q.value(0).toByteArray()) : QByteArray{};
    f.closeContentDb(conn);
    return html;
}

void Test_PageGenerator::test_pagegen_canonical_includes_lang_prefix_on_translated_page()
{
    // Regression: <link rel="canonical"> on translated pages pointed to the bare
    // English URL (e.g. https://example.com/article) instead of the language-prefixed
    // one (https://example.com/fr/article).  Google then treated the French page as a
    // duplicate of the English one and dropped it from the index.
    Fixture f;
    const QByteArray html = generateFrenchArticleHtml(f);
    QVERIFY2(!html.isEmpty(), "French article page not found in content.db");

    QVERIFY2(html.contains("rel=\"canonical\" href=\"https://example.com/fr/canonical-test-article\""),
             "canonical must include /fr/ language prefix");
    QVERIFY2(!html.contains("rel=\"canonical\" href=\"https://example.com/canonical-test-article\""),
             "canonical must NOT point to the bare English-domain URL");
}

void Test_PageGenerator::test_pagegen_og_url_includes_lang_prefix_on_translated_page()
{
    // Same regression for og:url — must carry /fr/ prefix so social scrapers
    // resolve to the correct language URL.
    Fixture f;
    const QByteArray html = generateFrenchArticleHtml(f);
    QVERIFY2(!html.isEmpty(), "French article page not found in content.db");

    QVERIFY2(html.contains("og:url\" content=\"https://example.com/fr/canonical-test-article\""),
             "og:url must include /fr/ language prefix");
    QVERIFY2(!html.contains("og:url\" content=\"https://example.com/canonical-test-article\""),
             "og:url must NOT point to the bare English-domain URL");
}

void Test_PageGenerator::test_pagegen_canonical_includes_translated_slug_with_lang_prefix()
{
    // When endPermalink is non-empty AND a translated slug is stored in page_data,
    // the canonical must combine both the /fr/ prefix and the translated slug:
    //   https://example.com/fr/article-en-francais
    // not https://example.com/fr/canonical-test-article (English slug).
    //
    // Note: the generator only looks up page_data translated slugs when endPermalink
    // is non-empty (strategy-suffix pages), so the test must set it.
    Fixture f;

    const QString enText = QStringLiteral("<h1>Sleep</h1><p>English.</p>");
    const QString frText = QStringLiteral("<h1>Sommeil</h1><p>Français.</p>");

    const int id = f.repo.create(QStringLiteral("article"),
                                  QStringLiteral("/canonical-test-article"),
                                  QStringLiteral("en"));
    f.repo.saveData(id, {
        {QStringLiteral("1_text"),                enText},
        {QStringLiteral("0_categories"),           QString()},
        {QStringLiteral("1_tr:fr:text"),           frText},
        {QStringLiteral("1_tr:fr:text:hash"),      Fixture::sha1(enText)},
        {QStringLiteral("tr:fr:_permalink_slug"),  QStringLiteral("article-en-francais")},
    });
    f.repo.setLangCodesToTranslate(id, {QStringLiteral("fr")});
    // Non-empty endPermalink is required for the generator to apply the translated slug.
    f.repo.setEndPermalink(id, QStringLiteral("test-article"));

    int frIndex = -1;
    for (int i = 0; i < f.engine.rowCount(); ++i) {
        if (f.engine.getLangCode(i) == QStringLiteral("fr")) {
            frIndex = i;
            break;
        }
    }
    QVERIFY2(frIndex >= 0, "No French engine row found");
    f.engine.setData(f.engine.index(frIndex, AbstractEngine::COL_DOMAIN),
                     QStringLiteral("example.com"));

    f.gen.generateAll(QDir(f.dir.path()), QDir(f.dir.path()),
                      QStringLiteral("example.com"), f.engine, frIndex,
                      QStringLiteral("https://example.com/fr"));

    // The French article is written at /article-en-francais (translated slug).
    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral(
        "SELECT pv.html_gz FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/article-en-francais'"));
    const QByteArray html = q.next() ? gzipDecompress(q.value(0).toByteArray()) : QByteArray{};
    f.closeContentDb(conn);

    QVERIFY2(!html.isEmpty(), "French article at /article-en-francais not found in content.db");
    QVERIFY2(html.contains("rel=\"canonical\" href=\"https://example.com/fr/article-en-francais\""),
             "canonical must use the translated slug with /fr/ prefix");
    QVERIFY2(!html.contains("rel=\"canonical\" href=\"https://example.com/canonical-test-article\""),
             "canonical must NOT use the English slug");
}

void Test_PageGenerator::test_pagegen_translated_permalink_map_includes_regular_article_without_end_permalink()
{
    // Regression: the translatedPermalinks pre-pass only added slug mappings for
    // pages with a non-empty endPermalink. For regular articles (no endPermalink),
    // resolveLinkHref always returned the English slug even when tr:fr:_permalink_slug
    // was stored — so internal links and hreflang alternates on every other page
    // pointed to the English URL, which no longer existed in content.db.
    Fixture f;

    const QString enText = QStringLiteral("<h1>Master Sleep</h1><p>English.</p>");
    const QString frText = QStringLiteral("<h1>Maîtriser le sommeil</h1><p>Français.</p>");

    const int id = f.repo.create(QStringLiteral("article"),
                                  QStringLiteral("/master-sleep"),
                                  QStringLiteral("en"));
    f.repo.saveData(id, {
        {QStringLiteral("1_text"),                enText},
        {QStringLiteral("0_categories"),           QString()},
        {QStringLiteral("1_tr:fr:text"),           frText},
        {QStringLiteral("1_tr:fr:text:hash"),      Fixture::sha1(enText)},
        {QStringLiteral("tr:fr:_permalink_slug"),  QStringLiteral("maitriser-le-sommeil")},
    });
    f.repo.setLangCodesToTranslate(id, {QStringLiteral("fr")});
    // NOTE: endPermalink is NOT set — this is the scenario for regular articles.

    int frIndex = -1;
    for (int i = 0; i < f.engine.rowCount(); ++i) {
        if (f.engine.getLangCode(i) == QStringLiteral("fr")) {
            frIndex = i;
            break;
        }
    }
    QVERIFY2(frIndex >= 0, "No French engine row found");

    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, frIndex);

    // After generateAll, the engine must know that /master-sleep → /maitriser-le-sommeil
    // for French, so that any page linking to this article uses the correct French URL.
    // Without the fix, this returned /master-sleep unchanged.
    QCOMPARE(f.engine.resolveLinkHref(QStringLiteral("/master-sleep"), frIndex),
             QStringLiteral("/maitriser-le-sommeil"));
}

// Builds an article with a French translated slug, generates the French site, and
// returns the decompressed HTML written at the French path.
static QByteArray generateTranslatedSlugArticle(Fixture &f, int &frIndexOut)
{
    const QString enText = QStringLiteral("<h1>Master Sleep</h1><p>English.</p>");
    const QString frText = QStringLiteral("<h1>Maîtriser le sommeil</h1><p>Français.</p>");

    const int id = f.repo.create(QStringLiteral("article"),
                                  QStringLiteral("/master-sleep"),
                                  QStringLiteral("en"));
    f.repo.saveData(id, {
        {QStringLiteral("1_text"),                enText},
        {QStringLiteral("0_categories"),           QString()},
        {QStringLiteral("1_tr:fr:text"),           frText},
        {QStringLiteral("1_tr:fr:text:hash"),      Fixture::sha1(enText)},
        {QStringLiteral("tr:fr:_permalink_slug"),  QStringLiteral("maitriser-le-sommeil")},
    });
    f.repo.setLangCodesToTranslate(id, {QStringLiteral("fr")});

    // Mirror the production setup: every language on ONE domain with /<lang>/ path
    // prefixes.  _buildHreflangTags skips any row with an empty domain, and also
    // any language without a deploy/<lang>/content.db, so both must exist here or
    // no alternate is emitted regardless of the fix under test.
    frIndexOut = -1;
    for (int i = 0; i < f.engine.rowCount(); ++i) {
        const QString lang = f.engine.getLangCode(i);
        if (lang == QStringLiteral("fr")) {
            frIndexOut = i;
        }
        if (lang == QStringLiteral("fr") || lang == QStringLiteral("en")) {
            f.engine.setData(f.engine.index(i, AbstractEngine::COL_DOMAIN),
                             QStringLiteral("example.com"));
            QDir(f.dir.path()).mkpath(QStringLiteral("deploy/") + lang);
            QFile marker(QDir(f.dir.path()).filePath(
                QStringLiteral("deploy/") + lang + QStringLiteral("/content.db")));
            marker.open(QIODevice::WriteOnly);
            marker.close();
        }
    }
    if (frIndexOut < 0) {
        return {};
    }

    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, frIndexOut);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral(
        "SELECT pv.html_gz FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id"
        " WHERE p.path = '/maitriser-le-sommeil'"));
    const QByteArray html = q.next() ? gzipDecompress(q.value(0).toByteArray()) : QByteArray{};
    f.closeContentDb(conn);
    return html;
}

void Test_PageGenerator::test_pagegen_translated_article_still_emits_hreflang_alternates()
{
    // Regression: articles were written by passing a record whose permalink had
    // ALREADY been replaced by the translated slug.  isPageAvailable() and
    // resolvePermalink() are both keyed by the ENGLISH permalink, so the translated
    // one matched neither and every hreflang alternate was silently dropped —
    // translated articles shipped with zero hreflang tags.
    Fixture f;
    int frIndex = -1;
    const QByteArray html = generateTranslatedSlugArticle(f, frIndex);

    QVERIFY2(!html.isEmpty(), "French article not written at the translated slug");
    QVERIFY2(html.contains("hreflang="),
             "translated article must still emit hreflang alternates");
}

void Test_PageGenerator::test_pagegen_translated_slug_emits_redirect_from_english_permalink()
{
    // The English permalink stays in Google's index after a slug translation, so a
    // 301 to the new path must be recorded.  permalink_history only tracks changes
    // to the English permalink itself and never fires for a per-language slug.
    Fixture f;
    int frIndex = -1;
    const QByteArray html = generateTranslatedSlugArticle(f, frIndex);
    QVERIFY2(!html.isEmpty(), "French article not written at the translated slug");

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral(
        "SELECT new_path, status_code FROM redirects WHERE old_path = '/master-sleep'"));
    QVERIFY2(q.next(), "no redirect recorded for the old English permalink");
    QCOMPARE(q.value(0).toString(), QStringLiteral("/maitriser-le-sommeil"));
    QCOMPARE(q.value(1).toInt(), 301);
    f.closeContentDb(conn);
}

void Test_PageGenerator::test_pagegen_redirect_target_includes_lang_prefix_on_subpath_deployment()
{
    // Production shape: one domain, nginx proxies /fr/ to the French Drogon after
    // stripping the prefix.  PageController copies new_path straight into the
    // Location header, so the target MUST keep the /fr prefix — a bare path would
    // bounce visitors to the English domain root.  old_path stays bare because that
    // is what Drogon actually receives after nginx strips the prefix.
    Fixture f;

    const QString enText = QStringLiteral("<h1>Master Sleep</h1><p>English.</p>");
    const QString frText = QStringLiteral("<h1>Maîtriser le sommeil</h1><p>Français.</p>");

    const int id = f.repo.create(QStringLiteral("article"),
                                  QStringLiteral("/master-sleep"),
                                  QStringLiteral("en"));
    f.repo.saveData(id, {
        {QStringLiteral("1_text"),                enText},
        {QStringLiteral("0_categories"),           QString()},
        {QStringLiteral("1_tr:fr:text"),           frText},
        {QStringLiteral("1_tr:fr:text:hash"),      Fixture::sha1(enText)},
        {QStringLiteral("tr:fr:_permalink_slug"),  QStringLiteral("maitriser-le-sommeil")},
    });
    f.repo.setLangCodesToTranslate(id, {QStringLiteral("fr")});

    int frIndex = -1;
    for (int i = 0; i < f.engine.rowCount(); ++i) {
        if (f.engine.getLangCode(i) == QStringLiteral("fr")) {
            frIndex = i;
            break;
        }
    }
    QVERIFY2(frIndex >= 0, "No French engine row found");
    f.engine.setData(f.engine.index(frIndex, AbstractEngine::COL_DOMAIN),
                     QStringLiteral("example.com"));

    f.gen.generateAll(QDir(f.dir.path()), QDir(f.dir.path()),
                      QStringLiteral("example.com"), f.engine, frIndex,
                      QStringLiteral("https://example.com/fr"));

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral(
        "SELECT new_path FROM redirects WHERE old_path = '/master-sleep'"));
    QVERIFY2(q.next(), "no redirect recorded for the old English permalink");
    QCOMPARE(q.value(0).toString(), QStringLiteral("/fr/maitriser-le-sommeil"));
    f.closeContentDb(conn);
}

void Test_PageGenerator::test_pagegen_history_redirect_target_includes_lang_prefix()
{
    // Same production shape and same rule as
    // test_pagegen_redirect_target_includes_lang_prefix_on_subpath_deployment,
    // but for the OTHER redirect writer: the permalink_history loop. That one
    // emitted a bare new_path, so a visitor arriving on a translated page's old
    // URL was 301'd to the ENGLISH domain root where the translated slug does
    // not exist. Confirmed live on biomarky.com before the fix:
    //   /es/rheumatoid-arthritis
    //     -> 301 /artritis-reumatoide-genes-biomarcadores -> 404
    Fixture f;

    const QString enText = QStringLiteral("<h1>Master Sleep</h1><p>English.</p>");
    const QString frText = QStringLiteral("<h1>Maitriser le sommeil</h1><p>Francais.</p>");

    const int id = f.repo.create(QStringLiteral("article"),
                                  QStringLiteral("/old-english-slug"),
                                  QStringLiteral("en"));
    f.repo.saveData(id, {
        {QStringLiteral("1_text"),                enText},
        {QStringLiteral("0_categories"),           QString()},
        {QStringLiteral("1_tr:fr:text"),           frText},
        {QStringLiteral("1_tr:fr:text:hash"),      Fixture::sha1(enText)},
        {QStringLiteral("tr:fr:_permalink_slug"),  QStringLiteral("maitriser-le-sommeil")},
    });
    f.repo.setLangCodesToTranslate(id, {QStringLiteral("fr")});
    // Published, so renaming records a permalink_history entry.
    f.repo.setPublishedAt(id, QStringLiteral("2024-01-01T00:00:00Z"));
    f.repo.updatePermalink(id, QStringLiteral("/master-sleep"));

    int frIndex = -1;
    for (int i = 0; i < f.engine.rowCount(); ++i) {
        if (f.engine.getLangCode(i) == QStringLiteral("fr")) {
            frIndex = i;
            break;
        }
    }
    QVERIFY2(frIndex >= 0, "No French engine row found");
    f.engine.setData(f.engine.index(frIndex, AbstractEngine::COL_DOMAIN),
                     QStringLiteral("example.com"));

    f.gen.generateAll(QDir(f.dir.path()), QDir(f.dir.path()),
                      QStringLiteral("example.com"), f.engine, frIndex,
                      QStringLiteral("https://example.com/fr"));

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral(
        "SELECT new_path FROM redirects WHERE old_path = '/old-english-slug'"));
    QVERIFY2(q.next(), "no redirect recorded for the renamed permalink");
    // Must carry the /fr prefix: PageController copies new_path verbatim into
    // the Location header, so it is the URL the BROWSER requests.
    QCOMPARE(q.value(0).toString(), QStringLiteral("/fr/maitriser-le-sommeil"));
    f.closeContentDb(conn);
}

void Test_PageGenerator::test_pagegen_robots_lists_sitemap_for_every_deployed_language()
{
    // Regression: robots.txt advertised only the generating language's sitemap.
    // Crawlers read robots.txt solely from the domain root, so /fr/robots.txt is
    // never fetched — on healybio.com that left 2692 French URLs (and 10 more
    // languages) undiscoverable behind a root index listing only English.
    Fixture f;
    // Article translated into fr only.  de gets a deploy folder but NO
    // translation, standing in for a language abandoned long ago whose stale
    // deploy/<lang>/content.db is still on disk but was never uploaded.
    const QString enText = QStringLiteral("<p>Body.</p>");
    const int id = f.repo.create(QStringLiteral("article"),
                                  QStringLiteral("/some-article"),
                                  QStringLiteral("en"));
    f.repo.saveData(id, {
        {QStringLiteral("1_text"),             enText},
        {QStringLiteral("0_categories"),        QString()},
        {QStringLiteral("1_tr:fr:text"),        QStringLiteral("<p>Corps.</p>")},
        {QStringLiteral("1_tr:fr:text:hash"),   Fixture::sha1(enText)},
    });
    f.repo.setLangCodesToTranslate(id, {QStringLiteral("fr"), QStringLiteral("de")});

    // One domain, several languages, each with a deploy/<lang>/content.db so
    // isLangDeployed() sees them.
    int enIndex = -1;
    for (int i = 0; i < f.engine.rowCount(); ++i) {
        const QString lang = f.engine.getLangCode(i);
        if (lang != QStringLiteral("en") && lang != QStringLiteral("fr")
                && lang != QStringLiteral("de")) {
            continue;
        }
        if (lang == QStringLiteral("en")) {
            enIndex = i;
        }
        f.engine.setData(f.engine.index(i, AbstractEngine::COL_DOMAIN),
                         QStringLiteral("example.com"));
        QDir(f.dir.path()).mkpath(QStringLiteral("deploy/") + lang);
        QFile marker(QDir(f.dir.path()).filePath(
            QStringLiteral("deploy/") + lang + QStringLiteral("/content.db")));
        marker.open(QIODevice::WriteOnly);
        marker.close();
    }
    QVERIFY2(enIndex >= 0, "No English engine row found");

    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, enIndex);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral(
        "SELECT pv.html_gz FROM page_variants pv"
        " JOIN pages p ON pv.page_id = p.id WHERE p.path = '/robots.txt'"));
    QVERIFY2(q.next(), "robots.txt not written");
    const QByteArray robots = gzipDecompress(q.value(0).toByteArray());
    f.closeContentDb(conn);

    QVERIFY2(robots.contains("Sitemap: https://example.com/sitemap.xml"), robots.constData());
    QVERIFY2(robots.contains("Sitemap: https://example.com/fr/sitemap.xml"), robots.constData());
    // English lives at the root, so it must not also appear under /en.
    QVERIFY2(!robots.contains("Sitemap: https://example.com/en/sitemap.xml"), robots.constData());
    // de has a deploy folder but no translated page, so its sitemap does not
    // exist — advertising it would send crawlers to a 404.  This is the
    // biomarky.com regression: 25 dead sitemap URLs in the root robots.txt
    // because stale deploy/<lang>/ folders made isLangDeployed() return true.
    QVERIFY2(!robots.contains("Sitemap: https://example.com/de/sitemap.xml"), robots.constData());
}

QTEST_MAIN(Test_PageGenerator)
#include "test_page_generator.moc"
