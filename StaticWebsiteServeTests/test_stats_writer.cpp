#include <drogon/drogon_test.h>

#include <filesystem>

#include "db/StatsDb.h"
#include "repository/StatsWriterSQLite.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Opens a fresh StatsDb at a unique temp path and returns both objects.
// The file is removed before construction to guarantee a clean state.
struct StatsFixture {
    std::string    path;
    StatsDb        db;
    StatsWriterSQLite writer;

    explicit StatsFixture(const std::string &suffix)
        : path((std::filesystem::temp_directory_path() / suffix).string())
        , db((std::filesystem::remove(path), path))
        , writer(db)
    {}

    ~StatsFixture() { std::filesystem::remove(path); }
};

// Reads a single column value from displays_clicks by row id.
std::string displayColumn(SQLite::Database &db, int64_t rowId, const std::string &col)
{
    SQLite::Statement q(db, "SELECT " + col + " FROM displays_clicks WHERE id = ?");
    q.bind(1, rowId);
    if (q.executeStep()) {
        return q.getColumn(0).isNull() ? std::string{} : q.getColumn(0).getString();
    }
    return std::string{};
}

int sessionCount(SQLite::Database &db)
{
    SQLite::Statement q(db, "SELECT COUNT(*) FROM page_session");
    q.executeStep();
    return q.getColumn(0).getInt();
}

} // namespace

// ---------------------------------------------------------------------------
// recordDisplay
// ---------------------------------------------------------------------------

DROGON_TEST(test_statswriter_record_display_inserts_row)
{
    StatsFixture f("test_sw_display_insert.db");

    const int64_t rowId = f.writer.recordDisplay("page-abc", "2026-04-01T10:00:00Z");

    SQLite::Statement q(f.db.database(),
        "SELECT COUNT(*) FROM displays_clicks WHERE id = ?");
    q.bind(1, rowId);
    q.executeStep();
    CHECK(q.getColumn(0).getInt() == 1);
}

DROGON_TEST(test_statswriter_record_display_stores_page_id)
{
    StatsFixture f("test_sw_display_page_id.db");

    const int64_t rowId = f.writer.recordDisplay("my-page", "2026-04-01T10:00:00Z");

    CHECK(displayColumn(f.db.database(), rowId, "page_id") == std::string("my-page"));
}

DROGON_TEST(test_statswriter_record_display_stores_display_at)
{
    StatsFixture f("test_sw_display_at.db");
    const std::string ts = "2026-04-01T10:00:00Z";

    const int64_t rowId = f.writer.recordDisplay("p", ts);

    CHECK(displayColumn(f.db.database(), rowId, "display_at") == ts);
}

DROGON_TEST(test_statswriter_record_display_clicked_at_initially_null)
{
    StatsFixture f("test_sw_display_null_click.db");

    const int64_t rowId = f.writer.recordDisplay("p", "2026-04-01T10:00:00Z");

    SQLite::Statement q(f.db.database(),
        "SELECT clicked_at FROM displays_clicks WHERE id = ?");
    q.bind(1, rowId);
    q.executeStep();
    CHECK(q.getColumn(0).isNull() == true);
}

DROGON_TEST(test_statswriter_record_display_returns_distinct_rowids)
{
    StatsFixture f("test_sw_display_distinct_ids.db");

    const int64_t id1 = f.writer.recordDisplay("p", "2026-04-01T10:00:00Z");
    const int64_t id2 = f.writer.recordDisplay("p", "2026-04-01T10:01:00Z");

    CHECK(id1 != id2);
}

// ---------------------------------------------------------------------------
// recordClick
// ---------------------------------------------------------------------------

DROGON_TEST(test_statswriter_record_click_sets_clicked_at)
{
    StatsFixture f("test_sw_click_sets.db");
    const int64_t rowId = f.writer.recordDisplay("p", "2026-04-01T10:00:00Z");
    const std::string clickTs = "2026-04-01T10:00:05Z";

    f.writer.recordClick(rowId, clickTs);

    CHECK(displayColumn(f.db.database(), rowId, "clicked_at") == clickTs);
}

DROGON_TEST(test_statswriter_record_click_does_not_touch_display_at)
{
    StatsFixture f("test_sw_click_keeps_display_at.db");
    const std::string displayTs = "2026-04-01T10:00:00Z";
    const int64_t rowId = f.writer.recordDisplay("p", displayTs);

    f.writer.recordClick(rowId, "2026-04-01T10:00:05Z");

    CHECK(displayColumn(f.db.database(), rowId, "display_at") == displayTs);
}

DROGON_TEST(test_statswriter_record_click_unknown_id_is_noop)
{
    StatsFixture f("test_sw_click_unknown.db");

    // Must not throw; no rows exist so the UPDATE affects zero rows.
    f.writer.recordClick(9999, "2026-04-01T10:00:05Z");
    CHECK(sessionCount(f.db.database()) == 0);  // unrelated table unchanged
}

DROGON_TEST(test_statswriter_record_click_only_updates_target_row)
{
    StatsFixture f("test_sw_click_target_only.db");
    const int64_t id1 = f.writer.recordDisplay("p", "2026-04-01T10:00:00Z");
    const int64_t id2 = f.writer.recordDisplay("p", "2026-04-01T10:01:00Z");

    f.writer.recordClick(id1, "2026-04-01T10:00:05Z");

    // id2 clicked_at must remain NULL.
    SQLite::Statement q(f.db.database(),
        "SELECT clicked_at FROM displays_clicks WHERE id = ?");
    q.bind(1, id2);
    q.executeStep();
    CHECK(q.getColumn(0).isNull() == true);
}

// ---------------------------------------------------------------------------
// recordSession
// ---------------------------------------------------------------------------

DROGON_TEST(test_statswriter_record_session_inserts_row)
{
    StatsFixture f("test_sw_session_insert.db");

    f.writer.recordSession("page-xyz", 75, 30, false);

    CHECK(sessionCount(f.db.database()) == 1);
}

DROGON_TEST(test_statswriter_record_session_stores_page_id)
{
    StatsFixture f("test_sw_session_page_id.db");

    f.writer.recordSession("my-page", 50, 10, false);

    SQLite::Statement q(f.db.database(),
        "SELECT page_id FROM page_session LIMIT 1");
    q.executeStep();
    CHECK(q.getColumn(0).getString() == std::string("my-page"));
}

DROGON_TEST(test_statswriter_record_session_stores_scrolling_percentage)
{
    StatsFixture f("test_sw_session_scroll.db");

    f.writer.recordSession("p", 42, 10, false);

    SQLite::Statement q(f.db.database(),
        "SELECT scrolling_percentage FROM page_session LIMIT 1");
    q.executeStep();
    CHECK(q.getColumn(0).getInt() == 42);
}

DROGON_TEST(test_statswriter_record_session_stores_time_on_page)
{
    StatsFixture f("test_sw_session_time.db");

    f.writer.recordSession("p", 10, 120, false);

    SQLite::Statement q(f.db.database(),
        "SELECT time_on_page FROM page_session LIMIT 1");
    q.executeStep();
    CHECK(q.getColumn(0).getInt() == 120);
}

DROGON_TEST(test_statswriter_record_session_stores_is_final_page_true)
{
    StatsFixture f("test_sw_session_final_true.db");

    f.writer.recordSession("p", 100, 60, true);

    SQLite::Statement q(f.db.database(),
        "SELECT is_final_page FROM page_session LIMIT 1");
    q.executeStep();
    CHECK(q.getColumn(0).getInt() == 1);
}

DROGON_TEST(test_statswriter_record_session_stores_is_final_page_false)
{
    StatsFixture f("test_sw_session_final_false.db");

    f.writer.recordSession("p", 0, 5, false);

    SQLite::Statement q(f.db.database(),
        "SELECT is_final_page FROM page_session LIMIT 1");
    q.executeStep();
    CHECK(q.getColumn(0).getInt() == 0);
}

DROGON_TEST(test_statswriter_record_session_multiple_rows)
{
    StatsFixture f("test_sw_session_multi.db");

    f.writer.recordSession("p1", 10, 5,  false);
    f.writer.recordSession("p2", 90, 60, true);
    f.writer.recordSession("p1", 50, 30, true);

    CHECK(sessionCount(f.db.database()) == 3);
}

DROGON_TEST(test_statswriter_record_session_scrolling_boundary_zero)
{
    StatsFixture f("test_sw_session_scroll_zero.db");

    // CHECK constraint: 0 is valid.
    f.writer.recordSession("p", 0, 1, false);

    CHECK(sessionCount(f.db.database()) == 1);
}

DROGON_TEST(test_statswriter_record_session_scrolling_boundary_hundred)
{
    StatsFixture f("test_sw_session_scroll_hundred.db");

    // CHECK constraint: 100 is valid.
    f.writer.recordSession("p", 100, 1, false);

    CHECK(sessionCount(f.db.database()) == 1);
}

int main(int argc, char *argv[])
{
    return drogon::test::run(argc, argv);
}
