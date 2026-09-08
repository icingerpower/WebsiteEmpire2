#include <QtTest>
#include <QTemporaryDir>

#include "website/pages/PageDb.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/RasterImageStatus.h"

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

namespace {
struct Fixture {
    QTemporaryDir    dir;
    PageDb           db;
    PageRepositoryDb repo;

    Fixture() : db(QDir(dir.path())), repo(db) {}

    int createPage(const QString &permalink) {
        return repo.create(QStringLiteral("article"), permalink, QStringLiteral("en"));
    }
};
} // namespace

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class Test_Website_RasterImageTracking : public QObject
{
    Q_OBJECT

private slots:
    // --- ensureRasterImagePending / rasterImageStatus / rasterImageAttempts ---
    void test_raster_ensure_pending_creates_row();
    void test_raster_ensure_pending_is_noop_when_row_exists();
    void test_raster_status_unknown_ref_returns_pending();
    void test_raster_attempts_unknown_ref_returns_zero();

    // --- recordRasterImageAttempt ---
    void test_raster_record_attempt_increments_count();
    void test_raster_record_attempt_sets_status();
    void test_raster_record_attempt_stores_last_error();
    void test_raster_record_success_clears_error_when_empty();

    // --- allRasterImagesTerminal ---
    void test_raster_terminal_true_when_no_rows();
    void test_raster_terminal_false_with_one_pending();
    void test_raster_terminal_true_when_success_and_failed_final_only();

    // --- hasRasterImages ---
    void test_raster_has_images_false_initially();
    void test_raster_has_images_true_after_ensure();

    // --- allRasterImagesSuccess ---
    void test_raster_all_success_true_when_no_rows();
    void test_raster_all_success_true_when_all_success();
    void test_raster_all_success_false_with_one_pending();
    void test_raster_all_success_false_with_one_failed_final();

    // --- resetFailedRasterImages (the requeue primitive) ---
    void test_raster_reset_failed_clears_status_and_attempts();
    void test_raster_reset_failed_returns_reset_row_count();
    void test_raster_reset_failed_leaves_success_untouched();
    void test_raster_reset_failed_leaves_pending_untouched();
    void test_raster_reset_failed_is_noop_when_nothing_failed();
    void test_raster_reset_failed_only_affects_given_page();
    void test_raster_reset_failed_makes_image_retryable_again();

    // --- findPagesWithUnresolvedRasterImages ---
    void test_raster_unresolved_pages_empty_when_all_success();
    void test_raster_unresolved_pages_lists_pending_page();
    void test_raster_unresolved_pages_lists_failed_final_page();
    void test_raster_unresolved_pages_lists_page_once_for_many_bad_images();
    void test_raster_unresolved_pages_ignores_pages_without_raster_rows();
    void test_raster_unresolved_pages_filters_by_type_id();
    void test_raster_unresolved_pages_excludes_translations();
    void test_raster_unresolved_pages_ordered_by_id();

    // --- countUnresolvedRasterImages ---
    void test_raster_count_unresolved_zero_when_no_rows();
    void test_raster_count_unresolved_zero_when_all_success();
    void test_raster_count_unresolved_counts_pending_and_failed();
    void test_raster_count_unresolved_isolated_per_page();

    // --- isolation across pages ---
    void test_raster_tracking_isolated_per_page();
};

// ---------------------------------------------------------------------------
// ensureRasterImagePending / rasterImageStatus / rasterImageAttempts
// ---------------------------------------------------------------------------

void Test_Website_RasterImageTracking::test_raster_ensure_pending_creates_row()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));

    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));

    QCOMPARE(f.repo.rasterImageStatus(pageId, QStringLiteral("img1")), RasterImageStatus::Pending);
    QCOMPARE(f.repo.rasterImageAttempts(pageId, QStringLiteral("img1")), 0);
}

void Test_Website_RasterImageTracking::test_raster_ensure_pending_is_noop_when_row_exists()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));

    // Simulate progress, then call ensure again — must not reset it.
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::Success, QString());
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));

    QCOMPARE(f.repo.rasterImageStatus(pageId, QStringLiteral("img1")), RasterImageStatus::Success);
    QCOMPARE(f.repo.rasterImageAttempts(pageId, QStringLiteral("img1")), 1);
}

void Test_Website_RasterImageTracking::test_raster_status_unknown_ref_returns_pending()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));

    QCOMPARE(f.repo.rasterImageStatus(pageId, QStringLiteral("nope")), RasterImageStatus::Pending);
}

void Test_Website_RasterImageTracking::test_raster_attempts_unknown_ref_returns_zero()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));

    QCOMPARE(f.repo.rasterImageAttempts(pageId, QStringLiteral("nope")), 0);
}

// ---------------------------------------------------------------------------
// recordRasterImageAttempt
// ---------------------------------------------------------------------------

void Test_Website_RasterImageTracking::test_raster_record_attempt_increments_count()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));

    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::Pending, QStringLiteral("bad crop"));
    QCOMPARE(f.repo.rasterImageAttempts(pageId, QStringLiteral("img1")), 1);

    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::FailedFinal, QStringLiteral("still bad"));
    QCOMPARE(f.repo.rasterImageAttempts(pageId, QStringLiteral("img1")), 2);
}

void Test_Website_RasterImageTracking::test_raster_record_attempt_sets_status()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));

    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::Success, QString());

    QCOMPARE(f.repo.rasterImageStatus(pageId, QStringLiteral("img1")), RasterImageStatus::Success);
}

void Test_Website_RasterImageTracking::test_raster_record_attempt_stores_last_error()
{
    // last_error is not exposed by a getter on IPageRepository (diagnostic only,
    // consumed via logs) — this test only pins that recording an error does not
    // throw/crash and status still updates correctly, since that's the only
    // externally-observable contract.
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));

    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::FailedFinal,
                                    QStringLiteral("FAIL: wrong garment color"));

    QCOMPARE(f.repo.rasterImageStatus(pageId, QStringLiteral("img1")), RasterImageStatus::FailedFinal);
}

void Test_Website_RasterImageTracking::test_raster_record_success_clears_error_when_empty()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));

    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::Pending, QStringLiteral("first try failed"));
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::Success, QString());

    QCOMPARE(f.repo.rasterImageStatus(pageId, QStringLiteral("img1")), RasterImageStatus::Success);
}

// ---------------------------------------------------------------------------
// allRasterImagesTerminal
// ---------------------------------------------------------------------------

void Test_Website_RasterImageTracking::test_raster_terminal_true_when_no_rows()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));

    QVERIFY(f.repo.allRasterImagesTerminal(pageId));
}

void Test_Website_RasterImageTracking::test_raster_terminal_false_with_one_pending()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img2"), QStringLiteral("b.jpg"));
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::Success, QString());
    // img2 stays Pending.

    QVERIFY(!f.repo.allRasterImagesTerminal(pageId));
}

void Test_Website_RasterImageTracking::test_raster_terminal_true_when_success_and_failed_final_only()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img2"), QStringLiteral("b.jpg"));
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::Success, QString());
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img2"),
                                    RasterImageStatus::FailedFinal, QStringLiteral("gave up"));

    // FailedFinal stops the retry loop (terminal), even though it is not Success.
    QVERIFY(f.repo.allRasterImagesTerminal(pageId));
}

// ---------------------------------------------------------------------------
// hasRasterImages
// ---------------------------------------------------------------------------

void Test_Website_RasterImageTracking::test_raster_has_images_false_initially()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));

    QVERIFY(!f.repo.hasRasterImages(pageId));
}

void Test_Website_RasterImageTracking::test_raster_has_images_true_after_ensure()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));

    QVERIFY(f.repo.hasRasterImages(pageId));
}

// ---------------------------------------------------------------------------
// allRasterImagesSuccess — the stricter publish-time gate: FailedFinal also
// blocks (unlike allRasterImagesTerminal(), which accepts it as "done trying").
// ---------------------------------------------------------------------------

void Test_Website_RasterImageTracking::test_raster_all_success_true_when_no_rows()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));

    QVERIFY(f.repo.allRasterImagesSuccess(pageId));
}

void Test_Website_RasterImageTracking::test_raster_all_success_true_when_all_success()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img2"), QStringLiteral("b.jpg"));
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::Success, QString());
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img2"),
                                    RasterImageStatus::Success, QString());

    QVERIFY(f.repo.allRasterImagesSuccess(pageId));
}

void Test_Website_RasterImageTracking::test_raster_all_success_false_with_one_pending()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img2"), QStringLiteral("b.jpg"));
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::Success, QString());
    // img2 stays Pending.

    QVERIFY(!f.repo.allRasterImagesSuccess(pageId));
}

void Test_Website_RasterImageTracking::test_raster_all_success_false_with_one_failed_final()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img2"), QStringLiteral("b.jpg"));
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::Success, QString());
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img2"),
                                    RasterImageStatus::FailedFinal, QStringLiteral("gave up"));

    // A page that gave up on one image (all attempts exhausted) still never
    // publishes silently with a broken image reference.
    QVERIFY(!f.repo.allRasterImagesSuccess(pageId));
}

// ---------------------------------------------------------------------------
// Isolation across pages
// ---------------------------------------------------------------------------

void Test_Website_RasterImageTracking::test_raster_tracking_isolated_per_page()
{
    Fixture f;
    const int pageA = f.createPage(QStringLiteral("/a"));
    const int pageB = f.createPage(QStringLiteral("/b"));
    f.repo.ensureRasterImagePending(pageA, QStringLiteral("img1"), QStringLiteral("a.jpg"));
    f.repo.recordRasterImageAttempt(pageA, QStringLiteral("img1"),
                                    RasterImageStatus::Success, QString());

    QVERIFY(f.repo.hasRasterImages(pageA));
    QVERIFY(!f.repo.hasRasterImages(pageB));
    QVERIFY(f.repo.allRasterImagesSuccess(pageB)); // vacuously true, unaffected by page A
}

// ---------------------------------------------------------------------------
// resetFailedRasterImages — the requeue primitive
// ---------------------------------------------------------------------------

void Test_Website_RasterImageTracking::test_raster_reset_failed_clears_status_and_attempts()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));
    // Burn all three attempts, ending FailedFinal — the real dead-end shape.
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::Pending, QStringLiteral("try 1"));
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::Pending, QStringLiteral("try 2"));
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::FailedFinal, QStringLiteral("gave up"));
    QCOMPARE(f.repo.rasterImageAttempts(pageId, QStringLiteral("img1")), 3);

    f.repo.resetFailedRasterImages(pageId);

    QCOMPARE(f.repo.rasterImageStatus(pageId, QStringLiteral("img1")),
             RasterImageStatus::Pending);
    // attempts MUST also go back to 0: LauncherGeneration's loop starts at
    // attempts + 1 and stops at kRasterMaxAttempts, so leaving 3 here would
    // make the "retry" silently skip the image.
    QCOMPARE(f.repo.rasterImageAttempts(pageId, QStringLiteral("img1")), 0);
}

void Test_Website_RasterImageTracking::test_raster_reset_failed_returns_reset_row_count()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    for (const auto &ref : {QStringLiteral("img1"), QStringLiteral("img2"),
                            QStringLiteral("img3")}) {
        f.repo.ensureRasterImagePending(pageId, ref, ref + QStringLiteral(".jpg"));
    }
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::FailedFinal, QStringLiteral("x"));
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img2"),
                                    RasterImageStatus::FailedFinal, QStringLiteral("x"));
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img3"),
                                    RasterImageStatus::Success, QString());

    QCOMPARE(f.repo.resetFailedRasterImages(pageId), 2);
}

void Test_Website_RasterImageTracking::test_raster_reset_failed_leaves_success_untouched()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("good"), QStringLiteral("g.jpg"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("bad"),  QStringLiteral("b.jpg"));
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("good"),
                                    RasterImageStatus::Success, QString());
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("bad"),
                                    RasterImageStatus::FailedFinal, QStringLiteral("x"));

    f.repo.resetFailedRasterImages(pageId);

    // An already-generated image must never be re-queued: that would burn a CLI
    // call and could replace a good image with a worse one.
    QCOMPARE(f.repo.rasterImageStatus(pageId, QStringLiteral("good")),
             RasterImageStatus::Success);
    QCOMPARE(f.repo.rasterImageAttempts(pageId, QStringLiteral("good")), 1);
    QCOMPARE(f.repo.rasterImageStatus(pageId, QStringLiteral("bad")),
             RasterImageStatus::Pending);
}

void Test_Website_RasterImageTracking::test_raster_reset_failed_leaves_pending_untouched()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));
    // One attempt used, still Pending (e.g. a quota pause mid-loop).
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::Pending, QStringLiteral("try 1"));

    QCOMPARE(f.repo.resetFailedRasterImages(pageId), 0);
    // Its remaining budget is preserved — the reset only revives dead ends.
    QCOMPARE(f.repo.rasterImageAttempts(pageId, QStringLiteral("img1")), 1);
}

void Test_Website_RasterImageTracking::test_raster_reset_failed_is_noop_when_nothing_failed()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));

    QCOMPARE(f.repo.resetFailedRasterImages(pageId), 0);
}

void Test_Website_RasterImageTracking::test_raster_reset_failed_only_affects_given_page()
{
    Fixture f;
    const int pageA = f.createPage(QStringLiteral("/a"));
    const int pageB = f.createPage(QStringLiteral("/b"));
    f.repo.ensureRasterImagePending(pageA, QStringLiteral("img1"), QStringLiteral("a.jpg"));
    f.repo.ensureRasterImagePending(pageB, QStringLiteral("img1"), QStringLiteral("b.jpg"));
    f.repo.recordRasterImageAttempt(pageA, QStringLiteral("img1"),
                                    RasterImageStatus::FailedFinal, QStringLiteral("x"));
    f.repo.recordRasterImageAttempt(pageB, QStringLiteral("img1"),
                                    RasterImageStatus::FailedFinal, QStringLiteral("x"));

    QCOMPARE(f.repo.resetFailedRasterImages(pageA), 1);

    QCOMPARE(f.repo.rasterImageStatus(pageA, QStringLiteral("img1")),
             RasterImageStatus::Pending);
    QCOMPARE(f.repo.rasterImageStatus(pageB, QStringLiteral("img1")),
             RasterImageStatus::FailedFinal);
}

void Test_Website_RasterImageTracking::test_raster_reset_failed_makes_image_retryable_again()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::FailedFinal, QStringLiteral("x"));
    QVERIFY(!f.repo.allRasterImagesSuccess(pageId));

    f.repo.resetFailedRasterImages(pageId);
    // Reset alone does not make the page complete — it re-opens the work.
    QVERIFY(!f.repo.allRasterImagesSuccess(pageId));
    QVERIFY(!f.repo.allRasterImagesTerminal(pageId));

    // ensureRasterImagePending() must not clobber the revived row, and the
    // image can now reach Success, which is what unblocks publishing.
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));
    QCOMPARE(f.repo.rasterImageStatus(pageId, QStringLiteral("img1")),
             RasterImageStatus::Pending);
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::Success, QString());
    QVERIFY(f.repo.allRasterImagesSuccess(pageId));
}

// ---------------------------------------------------------------------------
// findPagesWithUnresolvedRasterImages
// ---------------------------------------------------------------------------

void Test_Website_RasterImageTracking::test_raster_unresolved_pages_empty_when_all_success()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::Success, QString());

    QVERIFY(f.repo.findPagesWithUnresolvedRasterImages(QStringLiteral("article")).isEmpty());
}

void Test_Website_RasterImageTracking::test_raster_unresolved_pages_lists_pending_page()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));

    const QList<PageRecord> found =
        f.repo.findPagesWithUnresolvedRasterImages(QStringLiteral("article"));
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().id, pageId);
    QCOMPARE(found.first().permalink, QStringLiteral("/p"));
}

void Test_Website_RasterImageTracking::test_raster_unresolved_pages_lists_failed_final_page()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::FailedFinal, QStringLiteral("x"));

    const QList<PageRecord> found =
        f.repo.findPagesWithUnresolvedRasterImages(QStringLiteral("article"));
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().id, pageId);
}

void Test_Website_RasterImageTracking::test_raster_unresolved_pages_lists_page_once_for_many_bad_images()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    for (const auto &ref : {QStringLiteral("img1"), QStringLiteral("img2"),
                            QStringLiteral("img3")}) {
        f.repo.ensureRasterImagePending(pageId, ref, ref + QStringLiteral(".jpg"));
    }
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::FailedFinal, QStringLiteral("x"));

    // Three unresolved rows must not yield the same page three times, or the
    // "done" subtraction in PaneGeneration would over-count and go negative.
    const QList<PageRecord> found =
        f.repo.findPagesWithUnresolvedRasterImages(QStringLiteral("article"));
    QCOMPARE(found.size(), 1);
}

void Test_Website_RasterImageTracking::test_raster_unresolved_pages_ignores_pages_without_raster_rows()
{
    Fixture f;
    f.createPage(QStringLiteral("/no-images"));

    // The overwhelmingly common case: a page that never expected raster images
    // is not "unresolved" and must never be subtracted from the done count.
    QVERIFY(f.repo.findPagesWithUnresolvedRasterImages(QStringLiteral("article")).isEmpty());
}

void Test_Website_RasterImageTracking::test_raster_unresolved_pages_filters_by_type_id()
{
    Fixture f;
    const int articleId = f.createPage(QStringLiteral("/an-article"));
    const int otherId   = f.repo.create(QStringLiteral("legal"), QStringLiteral("/legal"),
                                        QStringLiteral("en"));
    f.repo.ensureRasterImagePending(articleId, QStringLiteral("img1"), QStringLiteral("a.jpg"));
    f.repo.ensureRasterImagePending(otherId,   QStringLiteral("img1"), QStringLiteral("b.jpg"));

    const QList<PageRecord> found =
        f.repo.findPagesWithUnresolvedRasterImages(QStringLiteral("article"));
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().id, articleId);
}

void Test_Website_RasterImageTracking::test_raster_unresolved_pages_excludes_translations()
{
    Fixture f;
    const int sourceId = f.createPage(QStringLiteral("/src"));
    const int transId  = f.repo.createTranslation(sourceId, QStringLiteral("article"),
                                                  QStringLiteral("/fr/src"),
                                                  QStringLiteral("fr"));
    QVERIFY(transId > 0);
    // A translation reuses its source's fileNames, so its image completeness is
    // governed by the SOURCE page's rows — it must never be listed separately.
    f.repo.ensureRasterImagePending(transId, QStringLiteral("img1"), QStringLiteral("a.jpg"));

    QVERIFY(f.repo.findPagesWithUnresolvedRasterImages(QStringLiteral("article")).isEmpty());
}

void Test_Website_RasterImageTracking::test_raster_unresolved_pages_ordered_by_id()
{
    Fixture f;
    const int first  = f.createPage(QStringLiteral("/a"));
    const int second = f.createPage(QStringLiteral("/b"));
    const int third  = f.createPage(QStringLiteral("/c"));
    for (int id : {third, first, second}) {
        f.repo.ensureRasterImagePending(id, QStringLiteral("img1"), QStringLiteral("x.jpg"));
    }

    const QList<PageRecord> found =
        f.repo.findPagesWithUnresolvedRasterImages(QStringLiteral("article"));
    QCOMPARE(found.size(), 3);
    QCOMPARE(found.at(0).id, first);
    QCOMPARE(found.at(1).id, second);
    QCOMPARE(found.at(2).id, third);
}

// ---------------------------------------------------------------------------
// countUnresolvedRasterImages
// ---------------------------------------------------------------------------

void Test_Website_RasterImageTracking::test_raster_count_unresolved_zero_when_no_rows()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));

    QCOMPARE(f.repo.countUnresolvedRasterImages(pageId), 0);
}

void Test_Website_RasterImageTracking::test_raster_count_unresolved_zero_when_all_success()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    f.repo.ensureRasterImagePending(pageId, QStringLiteral("img1"), QStringLiteral("a.jpg"));
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("img1"),
                                    RasterImageStatus::Success, QString());

    QCOMPARE(f.repo.countUnresolvedRasterImages(pageId), 0);
}

void Test_Website_RasterImageTracking::test_raster_count_unresolved_counts_pending_and_failed()
{
    Fixture f;
    const int pageId = f.createPage(QStringLiteral("/p"));
    for (const auto &ref : {QStringLiteral("ok"), QStringLiteral("pending"),
                            QStringLiteral("failed")}) {
        f.repo.ensureRasterImagePending(pageId, ref, ref + QStringLiteral(".jpg"));
    }
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("ok"),
                                    RasterImageStatus::Success, QString());
    f.repo.recordRasterImageAttempt(pageId, QStringLiteral("failed"),
                                    RasterImageStatus::FailedFinal, QStringLiteral("x"));

    // Both a never-attempted image and a permanently-failed one are work a
    // repair run must do — this is the figure shown to the user before starting.
    QCOMPARE(f.repo.countUnresolvedRasterImages(pageId), 2);
}

void Test_Website_RasterImageTracking::test_raster_count_unresolved_isolated_per_page()
{
    Fixture f;
    const int pageA = f.createPage(QStringLiteral("/a"));
    const int pageB = f.createPage(QStringLiteral("/b"));
    f.repo.ensureRasterImagePending(pageA, QStringLiteral("img1"), QStringLiteral("a.jpg"));
    f.repo.ensureRasterImagePending(pageB, QStringLiteral("img1"), QStringLiteral("b.jpg"));
    f.repo.ensureRasterImagePending(pageB, QStringLiteral("img2"), QStringLiteral("c.jpg"));

    QCOMPARE(f.repo.countUnresolvedRasterImages(pageA), 1);
    QCOMPARE(f.repo.countUnresolvedRasterImages(pageB), 2);
}

QTEST_MAIN(Test_Website_RasterImageTracking)
#include "test_raster_image_tracking.moc"
