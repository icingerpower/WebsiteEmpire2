#include <drogon/drogon_test.h>

#include <filesystem>

#include "db/ContentDb.h"
#include "db/ImageDb.h"
#include "db/StatsDb.h"

// ---------------------------------------------------------------------------
// ContentDb — pages table
// ---------------------------------------------------------------------------

DROGON_TEST(test_contentdb_pages_table_created)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_content_pages.db";
    std::filesystem::remove(path);

    ContentDb db(path);

    SQLite::Statement q(db.database(),
        "SELECT id, path, domain, lang, etag, updated_at FROM pages LIMIT 0");
    CHECK(q.executeStep() == false);  // table exists and schema is correct

    std::filesystem::remove(path);
}

DROGON_TEST(test_contentdb_page_variants_table_created)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_content_variants.db";
    std::filesystem::remove(path);

    ContentDb db(path);

    SQLite::Statement q(db.database(),
        "SELECT id, page_id, label, is_active, html_gz, etag FROM page_variants LIMIT 0");
    CHECK(q.executeStep() == false);

    std::filesystem::remove(path);
}

DROGON_TEST(test_contentdb_menu_fragments_table_created)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_content_menu.db";
    std::filesystem::remove(path);

    ContentDb db(path);

    SQLite::Statement q(db.database(),
        "SELECT id, domain, lang, html_gz, version_id, updated_at FROM menu_fragments LIMIT 0");
    CHECK(q.executeStep() == false);

    std::filesystem::remove(path);
}

DROGON_TEST(test_contentdb_redirects_table_created)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_content_redirects.db";
    std::filesystem::remove(path);

    ContentDb db(path);

    SQLite::Statement q(db.database(),
        "SELECT old_path, new_path, status_code FROM redirects LIMIT 0");
    CHECK(q.executeStep() == false);

    std::filesystem::remove(path);
}

DROGON_TEST(test_contentdb_schema_idempotent)
{
    // Opening the same DB twice must not throw (CREATE TABLE IF NOT EXISTS).
    const std::string path = std::filesystem::temp_directory_path() / "test_content_idempotent.db";
    std::filesystem::remove(path);

    {
        ContentDb db(path);
    }
    {
        ContentDb db(path);  // second open — must not throw
        SQLite::Statement q(db.database(), "SELECT COUNT(*) FROM pages");
        CHECK(q.executeStep());
    }

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// ImageDb
// ---------------------------------------------------------------------------

DROGON_TEST(test_imagedb_schema_created_on_open)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_images.db";
    std::filesystem::remove(path);

    ImageDb db(path);

    SQLite::Statement q1(db.database(), "SELECT COUNT(*) FROM images");
    CHECK(q1.executeStep());

    SQLite::Statement q2(db.database(), "SELECT COUNT(*) FROM image_names");
    CHECK(q2.executeStep());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// StatsDb
// ---------------------------------------------------------------------------

DROGON_TEST(test_statsdb_schema_created_on_open)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_stats.db";
    std::filesystem::remove(path);

    StatsDb db(path);

    SQLite::Statement q1(db.database(), "SELECT COUNT(*) FROM displays_clicks");
    CHECK(q1.executeStep());

    SQLite::Statement q2(db.database(), "SELECT COUNT(*) FROM page_session");
    CHECK(q2.executeStep());

    std::filesystem::remove(path);
}

DROGON_TEST(test_statsdb_wal_mode_enabled)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_stats_wal.db";
    std::filesystem::remove(path);

    StatsDb db(path);

    SQLite::Statement q(db.database(), "PRAGMA journal_mode");
    REQUIRE(q.executeStep());
    CHECK(q.getColumn(0).getString() == std::string("wal"));

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    return drogon::test::run(argc, argv);
}
