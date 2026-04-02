#include <drogon/drogon_test.h>

#include <algorithm>
#include <filesystem>
#include <vector>

#include "db/ContentDb.h"
#include "db/ImageDb.h"
#include "repository/ImageRepositorySQLite.h"
#include "repository/MenuRepositorySQLite.h"
#include "repository/PageRepositorySQLite.h"
#include "repository/RedirectRepositorySQLite.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const std::vector<uint8_t> kFakeGz  = {0x1f, 0x8b, 0x08, 0x00};
static const std::vector<uint8_t> kFakeGz2 = {0x1f, 0x8b, 0x08, 0x01};

/** Insert a page row and return its INTEGER rowid. */
static int64_t insertPage(SQLite::Database  &db,
                           const std::string &path,
                           const std::string &domain,
                           const std::string &lang,
                           const std::string &etag,
                           const std::string &updatedAt)
{
    SQLite::Statement s(db,
        "INSERT INTO pages (path, domain, lang, etag, updated_at)"
        " VALUES (?, ?, ?, ?, ?)");
    s.bind(1, path);
    s.bind(2, domain);
    s.bind(3, lang);
    s.bind(4, etag);
    s.bind(5, updatedAt);
    s.exec();
    return db.getLastInsertRowid();
}

/** Insert a page_variant row. */
static void insertVariant(SQLite::Database         &db,
                           int64_t                   pageId,
                           const std::string        &label,
                           int                       isActive,
                           const std::vector<uint8_t> &htmlGz,
                           const std::string        &etag)
{
    SQLite::Statement s(db,
        "INSERT INTO page_variants (page_id, label, is_active, html_gz, etag)"
        " VALUES (?, ?, ?, ?, ?)");
    s.bind(1, pageId);
    s.bind(2, label);
    s.bind(3, isActive);
    s.bind(4, static_cast<const void *>(htmlGz.data()), static_cast<int>(htmlGz.size()));
    s.bind(5, etag);
    s.exec();
}

// ---------------------------------------------------------------------------
// PageRepositorySQLite — findByPath
// ---------------------------------------------------------------------------

DROGON_TEST(test_pagerepository_findbypath_returns_nullopt_when_empty)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_pr_empty.db";
    std::filesystem::remove(path);

    ContentDb            db(path);
    PageRepositorySQLite repo(db);

    CHECK(!repo.findByPath("/nonexistent.html").has_value());

    std::filesystem::remove(path);
}

DROGON_TEST(test_pagerepository_findbypath_returns_page)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_pr_findbypath.db";
    std::filesystem::remove(path);

    ContentDb db(path);
    const int64_t id = insertPage(db.database(), "/article.html", "example.com", "en",
                                   "etag1", "2026-01-01");

    PageRepositorySQLite repo(db);
    const auto           result = repo.findByPath("/article.html");

    REQUIRE(result.has_value());
    CHECK(result->id        == id);
    CHECK(result->path      == "/article.html");
    CHECK(result->domain    == "example.com");
    CHECK(result->lang      == "en");
    CHECK(result->etag      == "etag1");
    CHECK(result->updatedAt == "2026-01-01");

    std::filesystem::remove(path);
}

DROGON_TEST(test_pagerepository_findbypath_returns_nullopt_for_wrong_path)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_pr_wrongpath.db";
    std::filesystem::remove(path);

    ContentDb db(path);
    insertPage(db.database(), "/real.html", "example.com", "en", "e", "2026-01-01");

    PageRepositorySQLite repo(db);
    CHECK(!repo.findByPath("/other.html").has_value());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// PageRepositorySQLite — findVariant
// ---------------------------------------------------------------------------

DROGON_TEST(test_pagerepository_findvariant_returns_nullopt_when_empty)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_pr_variant_empty.db";
    std::filesystem::remove(path);

    ContentDb            db(path);
    const int64_t        pageId = insertPage(db.database(), "/p.html", "d.com", "en", "e", "2026-01-01");
    PageRepositorySQLite repo(db);

    CHECK(!repo.findVariant(pageId, "control").has_value());

    std::filesystem::remove(path);
}

DROGON_TEST(test_pagerepository_findvariant_returns_control_variant)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_pr_variant_control.db";
    std::filesystem::remove(path);

    ContentDb     db(path);
    const int64_t pageId = insertPage(db.database(), "/p.html", "d.com", "en", "e", "2026-01-01");
    insertVariant(db.database(), pageId, "control", 1, kFakeGz, "v1");

    PageRepositorySQLite repo(db);
    const auto           result = repo.findVariant(pageId, "control");

    REQUIRE(result.has_value());
    CHECK(result->label    == "control");
    CHECK(result->isActive == true);
    CHECK(result->htmlGz   == kFakeGz);
    CHECK(result->etag     == "v1");

    std::filesystem::remove(path);
}

DROGON_TEST(test_pagerepository_findvariant_skips_inactive_variant)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_pr_variant_inactive.db";
    std::filesystem::remove(path);

    ContentDb     db(path);
    const int64_t pageId = insertPage(db.database(), "/p.html", "d.com", "en", "e", "2026-01-01");
    insertVariant(db.database(), pageId, "control", 0 /* is_active=0 */, kFakeGz, "v1");

    PageRepositorySQLite repo(db);
    CHECK(!repo.findVariant(pageId, "control").has_value());

    std::filesystem::remove(path);
}

DROGON_TEST(test_pagerepository_findvariant_returns_ab_variant)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_pr_variant_ab.db";
    std::filesystem::remove(path);

    ContentDb     db(path);
    const int64_t pageId = insertPage(db.database(), "/p.html", "d.com", "en", "e", "2026-01-01");
    insertVariant(db.database(), pageId, "control", 1, kFakeGz,  "v1");
    insertVariant(db.database(), pageId, "a",       1, kFakeGz2, "v2");

    PageRepositorySQLite repo(db);
    const auto           va = repo.findVariant(pageId, "a");

    REQUIRE(va.has_value());
    CHECK(va->label  == "a");
    CHECK(va->htmlGz == kFakeGz2);
    CHECK(va->etag   == "v2");

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// PageRepositorySQLite — findActiveVariantLabels
// ---------------------------------------------------------------------------

DROGON_TEST(test_pagerepository_findactivevariantlabels_returns_empty_for_no_variants)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_pr_labels_empty.db";
    std::filesystem::remove(path);

    ContentDb     db(path);
    const int64_t pageId = insertPage(db.database(), "/p.html", "d.com", "en", "e", "2026-01-01");

    PageRepositorySQLite     repo(db);
    const auto               labels = repo.findActiveVariantLabels(pageId);
    CHECK(labels.empty());

    std::filesystem::remove(path);
}

DROGON_TEST(test_pagerepository_findactivevariantlabels_returns_active_labels)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_pr_labels_active.db";
    std::filesystem::remove(path);

    ContentDb     db(path);
    const int64_t pageId = insertPage(db.database(), "/p.html", "d.com", "en", "e", "2026-01-01");
    insertVariant(db.database(), pageId, "control", 1, kFakeGz,  "v1");
    insertVariant(db.database(), pageId, "a",       1, kFakeGz2, "v2");
    insertVariant(db.database(), pageId, "b",       0, kFakeGz,  "v3");  // inactive

    PageRepositorySQLite repo(db);
    const auto           labels = repo.findActiveVariantLabels(pageId);

    CHECK(labels.size() == 2u);
    const bool hasControl = std::find(labels.begin(), labels.end(), "control") != labels.end();
    const bool hasA       = std::find(labels.begin(), labels.end(), "a")       != labels.end();
    const bool hasB       = std::find(labels.begin(), labels.end(), "b")       != labels.end();
    CHECK(hasControl);
    CHECK(hasA);
    CHECK(!hasB);

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// MenuRepositorySQLite
// ---------------------------------------------------------------------------

DROGON_TEST(test_menurepository_findbydomainandlang_returns_nullopt_when_empty)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_menu_empty.db";
    std::filesystem::remove(path);

    ContentDb             db(path);
    MenuRepositorySQLite  repo(db);
    CHECK(!repo.findByDomainAndLang("example.com", "en").has_value());

    std::filesystem::remove(path);
}

DROGON_TEST(test_menurepository_findbydomainandlang_returns_fragment)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_menu_find.db";
    std::filesystem::remove(path);

    ContentDb db(path);
    {
        SQLite::Statement s(db.database(),
            "INSERT INTO menu_fragments (domain, lang, html_gz, version_id, updated_at)"
            " VALUES (?, ?, ?, ?, ?)");
        s.bind(1, "example.com");
        s.bind(2, "en");
        s.bind(3, static_cast<const void *>(kFakeGz.data()), static_cast<int>(kFakeGz.size()));
        s.bind(4, "ver1");
        s.bind(5, "2026-01-01");
        s.exec();
    }

    MenuRepositorySQLite repo(db);
    const auto           result = repo.findByDomainAndLang("example.com", "en");

    REQUIRE(result.has_value());
    CHECK(result->domain    == "example.com");
    CHECK(result->lang      == "en");
    CHECK(result->htmlGz    == kFakeGz);
    CHECK(result->versionId == "ver1");
    CHECK(result->updatedAt == "2026-01-01");

    std::filesystem::remove(path);
}

DROGON_TEST(test_menurepository_findall_returns_all_fragments)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_menu_findall.db";
    std::filesystem::remove(path);

    ContentDb db(path);
    for (const auto &[domain, lang] :
         std::initializer_list<std::pair<const char *, const char *>>{
             {"example.com", "en"},
             {"example.com", "fr"},
             {"other.com",   "en"}})
    {
        SQLite::Statement s(db.database(),
            "INSERT INTO menu_fragments (domain, lang, html_gz, version_id, updated_at)"
            " VALUES (?, ?, ?, ?, ?)");
        s.bind(1, domain);
        s.bind(2, lang);
        s.bind(3, static_cast<const void *>(kFakeGz.data()), static_cast<int>(kFakeGz.size()));
        s.bind(4, "v1");
        s.bind(5, "2026-01-01");
        s.exec();
    }

    MenuRepositorySQLite    repo(db);
    const auto              all = repo.findAll();
    CHECK(all.size() == 3u);

    std::filesystem::remove(path);
}

DROGON_TEST(test_menurepository_findbydomainandlang_returns_nullopt_for_wrong_lang)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_menu_wronglang.db";
    std::filesystem::remove(path);

    ContentDb db(path);
    {
        SQLite::Statement s(db.database(),
            "INSERT INTO menu_fragments (domain, lang, html_gz, version_id, updated_at)"
            " VALUES ('example.com', 'en', ?, 'v1', '2026-01-01')");
        s.bind(1, static_cast<const void *>(kFakeGz.data()), static_cast<int>(kFakeGz.size()));
        s.exec();
    }

    MenuRepositorySQLite repo(db);
    CHECK(!repo.findByDomainAndLang("example.com", "fr").has_value());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// RedirectRepositorySQLite
// ---------------------------------------------------------------------------

DROGON_TEST(test_redirectrepository_findbypath_returns_nullopt_when_empty)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_redir_empty.db";
    std::filesystem::remove(path);

    ContentDb                db(path);
    RedirectRepositorySQLite repo(db);
    CHECK(!repo.findByPath("/old.html").has_value());

    std::filesystem::remove(path);
}

DROGON_TEST(test_redirectrepository_findbypath_returns_301)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_redir_301.db";
    std::filesystem::remove(path);

    ContentDb db(path);
    db.database().exec(
        "INSERT INTO redirects (old_path, new_path, status_code)"
        " VALUES ('/old.html', '/new.html', 301)");

    RedirectRepositorySQLite repo(db);
    const auto               result = repo.findByPath("/old.html");

    REQUIRE(result.has_value());
    CHECK(result->oldPath    == "/old.html");
    CHECK(result->newPath    == "/new.html");
    CHECK(result->statusCode == 301);

    std::filesystem::remove(path);
}

DROGON_TEST(test_redirectrepository_findbypath_returns_410)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_redir_410.db";
    std::filesystem::remove(path);

    ContentDb db(path);
    db.database().exec(
        "INSERT INTO redirects (old_path, new_path, status_code)"
        " VALUES ('/gone.html', NULL, 410)");

    RedirectRepositorySQLite repo(db);
    const auto               result = repo.findByPath("/gone.html");

    REQUIRE(result.has_value());
    CHECK(result->oldPath    == "/gone.html");
    CHECK(result->statusCode == 410);
    CHECK(result->newPath.empty());

    std::filesystem::remove(path);
}

DROGON_TEST(test_redirectrepository_findbypath_returns_nullopt_for_unknown_path)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_redir_unknown.db";
    std::filesystem::remove(path);

    ContentDb db(path);
    db.database().exec(
        "INSERT INTO redirects (old_path, new_path, status_code)"
        " VALUES ('/old.html', '/new.html', 301)");

    RedirectRepositorySQLite repo(db);
    CHECK(!repo.findByPath("/different.html").has_value());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// ImageRepositorySQLite
// ---------------------------------------------------------------------------

DROGON_TEST(test_imagerepository_findbydomainandfilename_returns_nullopt_when_empty)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_img_empty.db";
    std::filesystem::remove(path);

    ImageDb                 db(path);
    ImageRepositorySQLite   repo(db);
    CHECK(!repo.findByDomainAndFilename("example.com", "hero.webp").has_value());

    std::filesystem::remove(path);
}

DROGON_TEST(test_imagerepository_findbydomainandfilename_returns_image)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_img_find.db";
    std::filesystem::remove(path);

    const std::vector<uint8_t> fakeBlob = {0xff, 0xd8, 0x00, 0x01};

    ImageDb db(path);
    {
        SQLite::Statement s(db.database(),
            "INSERT INTO images (blob, mime_type) VALUES (?, ?)");
        s.bind(1, static_cast<const void *>(fakeBlob.data()), static_cast<int>(fakeBlob.size()));
        s.bind(2, "image/webp");
        s.exec();
    }
    const int64_t imageId = db.database().getLastInsertRowid();
    {
        SQLite::Statement s(db.database(),
            "INSERT INTO image_names (domain, filename, image_id) VALUES (?, ?, ?)");
        s.bind(1, "example.com");
        s.bind(2, "hero.webp");
        s.bind(3, imageId);
        s.exec();
    }

    ImageRepositorySQLite repo(db);
    const auto            result = repo.findByDomainAndFilename("example.com", "hero.webp");

    REQUIRE(result.has_value());
    CHECK(result->mimeType == "image/webp");
    CHECK(result->blob     == fakeBlob);

    std::filesystem::remove(path);
}

DROGON_TEST(test_imagerepository_findbydomainandfilename_returns_nullopt_for_wrong_domain)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_img_wrongdomain.db";
    std::filesystem::remove(path);

    const std::vector<uint8_t> fakeBlob = {0x00};

    ImageDb db(path);
    {
        SQLite::Statement s(db.database(),
            "INSERT INTO images (blob, mime_type) VALUES (?, 'image/png')");
        s.bind(1, static_cast<const void *>(fakeBlob.data()), static_cast<int>(fakeBlob.size()));
        s.exec();
    }
    const int64_t imageId = db.database().getLastInsertRowid();
    {
        SQLite::Statement s(db.database(),
            "INSERT INTO image_names (domain, filename, image_id) VALUES (?, ?, ?)");
        s.bind(1, "example.com");
        s.bind(2, "logo.png");
        s.bind(3, imageId);
        s.exec();
    }

    ImageRepositorySQLite repo(db);
    CHECK(!repo.findByDomainAndFilename("other.com", "logo.png").has_value());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    return drogon::test::run(argc, argv);
}
