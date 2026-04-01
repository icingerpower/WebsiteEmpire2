#include <drogon/drogon_test.h>

#include <filesystem>

#include "db/ContentDb.h"
#include "db/ImageDb.h"
#include "db/StatsDb.h"

// ---------------------------------------------------------------------------
// ContentDb
// ---------------------------------------------------------------------------

DROGON_TEST(test_contentdb_schema_created_on_open)
{
    const std::string path = std::filesystem::temp_directory_path() / "test_content.db";
    std::filesystem::remove(path);

    ContentDb db(path);

    // Schema must exist — querying the table must not throw.
    SQLite::Statement q(db.database(), "SELECT COUNT(*) FROM pages");
    CHECK(q.executeStep());

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
