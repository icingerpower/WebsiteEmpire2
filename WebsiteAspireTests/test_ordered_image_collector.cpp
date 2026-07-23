#include <QtTest>

#include <QImage>
#include <QList>
#include <QSharedPointer>
#include <QString>
#include <QStringList>

#include "aspire/downloader/OrderedImageCollector.h"
#include "aspire/attributes/PageAttributesProduct.h"

// ---------------------------------------------------------------------------
// Regression test for the "images stored in network-completion order" bug.
//
// Previously each downloaded image was APPENDED to a shared list in the order
// the replies happened to finish, so the stored order was a permutation of the
// source URL order (the source's 1st image could end up last). A failed decode
// was also silently dropped, shifting every later image. OrderedImageCollector
// fixes both by using the URL index as the storage slot (position == identity)
// and by compacting failures out while preserving the surviving order.
//
// These tests inject completions OUT OF ORDER and assert the resulting list is
// in URL order. Each test image is tagged by giving it a unique width equal to
// its intended URL index (plus an offset), so the assembled order is verifiable
// by reading widths.
// ---------------------------------------------------------------------------

// Captures qWarning output so the test can assert that a failure was surfaced
// visibly rather than swallowed (§XV-1 "no invisible failure").
static QStringList *s_capturedWarnings = nullptr;
static void capturingMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    if (type == QtWarningMsg && s_capturedWarnings) {
        s_capturedWarnings->append(msg);
    }
}

class Test_Aspire_OrderedImageCollector : public QObject
{
    Q_OBJECT

private:
    // Builds a 1-pixel-tall image whose WIDTH encodes its identity.
    static QSharedPointer<QImage> taggedImage(int width);
    // Builds an image of the given size whose WIDTH still encodes its identity,
    // so min-size filtering tests can vary the smallest side independently.
    static QSharedPointer<QImage> taggedImage(int width, int height);

private slots:
    void test_collector_preserves_url_order_when_replies_complete_out_of_order();
    void test_collector_completes_after_last_reply_only();
    void test_collector_failed_image_dropped_keeps_relative_order();
    void test_collector_failure_is_surfaced_with_url();
    void test_collector_all_failures_yield_empty_list_without_placeholder();
    // Minimum-size policy (approved 2026-07): drop only the too-small image(s),
    // keep the product with its remaining valid images, in original order.
    void test_collector_min_size_drops_small_image_keeps_valid_in_order();
    void test_collector_min_size_drop_is_surfaced_with_url();
    void test_collector_all_below_min_size_yield_empty_list_with_reason();
};

QSharedPointer<QImage> Test_Aspire_OrderedImageCollector::taggedImage(int width)
{
    auto img = QSharedPointer<QImage>::create(width, 1, QImage::Format_RGB32);
    img->fill(Qt::black);
    return img;
}

QSharedPointer<QImage> Test_Aspire_OrderedImageCollector::taggedImage(int width, int height)
{
    auto img = QSharedPointer<QImage>::create(width, height, QImage::Format_RGB32);
    img->fill(Qt::black);
    return img;
}

// URLs in order [0,1,2,3]; replies complete in scrambled order [2,0,3,1].
// The assembled list must still be [0,1,2,3] (widths 10,11,12,13).
void Test_Aspire_OrderedImageCollector::test_collector_preserves_url_order_when_replies_complete_out_of_order()
{
    QList<QSharedPointer<QImage>> received;
    bool done = false;

    OrderedImageCollector collector(4, [&](QList<QSharedPointer<QImage>> images) {
        received = images;
        done = true;
    });

    // Deliberately out of order.
    collector.setResult(2, taggedImage(12));
    collector.setResult(0, taggedImage(10));
    collector.setResult(3, taggedImage(13));
    QVERIFY(!done); // must not complete before the last reply
    collector.setResult(1, taggedImage(11));

    QVERIFY(done);
    QCOMPARE(received.size(), 4);
    QCOMPARE(received.at(0)->width(), 10);
    QCOMPARE(received.at(1)->width(), 11);
    QCOMPARE(received.at(2)->width(), 12);
    QCOMPARE(received.at(3)->width(), 13);
}

// onComplete must fire exactly once, and only after every slot is accounted for.
void Test_Aspire_OrderedImageCollector::test_collector_completes_after_last_reply_only()
{
    int completions = 0;

    OrderedImageCollector collector(3, [&](QList<QSharedPointer<QImage>>) {
        ++completions;
    });

    collector.setResult(1, taggedImage(1));
    QCOMPARE(completions, 0);
    collector.setResult(2, taggedImage(2));
    QCOMPARE(completions, 0);
    collector.setResult(0, taggedImage(3));
    QCOMPARE(completions, 1);
}

// Index 2 of 5 fails. Remaining images must keep their relative order and the
// failed slot must NOT shift them (no positional corruption, no placeholder).
void Test_Aspire_OrderedImageCollector::test_collector_failed_image_dropped_keeps_relative_order()
{
    QList<QSharedPointer<QImage>> received;

    OrderedImageCollector collector(5, [&](QList<QSharedPointer<QImage>> images) {
        received = images;
    });

    // Out-of-order completion, with index 2 failing.
    collector.setResult(4, taggedImage(24));
    collector.setResult(0, taggedImage(20));
    collector.setFailure(2, QStringLiteral("https://cdn.example.com/broken.jpg"));
    collector.setResult(3, taggedImage(23));
    collector.setResult(1, taggedImage(21));

    // 4 survivors, in URL order 0,1,3,4 — index 2 removed without shifting.
    QCOMPARE(received.size(), 4);
    QCOMPARE(received.at(0)->width(), 20);
    QCOMPARE(received.at(1)->width(), 21);
    QCOMPARE(received.at(2)->width(), 23);
    QCOMPARE(received.at(3)->width(), 24);
}

// A failure must be logged visibly (qWarning) and include the offending URL.
void Test_Aspire_OrderedImageCollector::test_collector_failure_is_surfaced_with_url()
{
    QStringList warnings;
    s_capturedWarnings = &warnings;
    QtMessageHandler previous = qInstallMessageHandler(capturingMessageHandler);

    const QString badUrl = QStringLiteral("https://cdn.example.com/missing-primary.png");
    {
        OrderedImageCollector collector(2, [&](QList<QSharedPointer<QImage>>) {});
        collector.setResult(0, taggedImage(1));
        collector.setFailure(1, badUrl);
    }

    qInstallMessageHandler(previous);
    s_capturedWarnings = nullptr;

    bool surfaced = false;
    for (const QString &w : std::as_const(warnings)) {
        if (w.contains(badUrl)) {
            surfaced = true;
            break;
        }
    }
    QVERIFY2(surfaced, "failed image URL was not surfaced in a qWarning");
}

// When every download fails, the callback receives an EMPTY list — the code
// must not fabricate a placeholder to hide the failure (§XV-1).
void Test_Aspire_OrderedImageCollector::test_collector_all_failures_yield_empty_list_without_placeholder()
{
    QList<QSharedPointer<QImage>> received;
    bool done = false;

    OrderedImageCollector collector(3, [&](QList<QSharedPointer<QImage>> images) {
        received = images;
        done = true;
    });

    collector.setFailure(2, QStringLiteral("https://cdn.example.com/a.jpg"));
    collector.setFailure(0, QStringLiteral("https://cdn.example.com/b.jpg"));
    collector.setFailure(1, QStringLiteral("https://cdn.example.com/c.jpg"));

    QVERIFY(done);
    QVERIFY(received.isEmpty());
}

// A product with valid images plus ONE sub-threshold image must record only the
// valid images, in their original relative order, and must NOT be dropped as a
// whole (approved 2026-07). The small image at index 1 (min side 50 < 200) is
// compacted out exactly like a failed slot, without shifting the survivors.
void Test_Aspire_OrderedImageCollector::test_collector_min_size_drops_small_image_keeps_valid_in_order()
{
    QList<QSharedPointer<QImage>> received;
    bool done = false;

    const QStringList urls{
        QStringLiteral("https://cdn.example.com/a.jpg"),
        QStringLiteral("https://cdn.example.com/too-small.jpg"),
        QStringLiteral("https://cdn.example.com/c.jpg")};

    OrderedImageCollector collector(
        urls.size(),
        [&](QList<QSharedPointer<QImage>> images) {
            received = images;
            done = true;
        },
        PageAttributesProduct::MIN_IMAGE_SIDE_PX,
        urls,
        QStringLiteral("https://shop.example.com/product/42"));

    // Out-of-order completion; index 1 is a valid decode but too small.
    collector.setResult(2, taggedImage(260, 300));                        // valid
    collector.setResult(1, taggedImage(50, 300));                         // 50 < 200 → dropped
    QVERIFY(!done);
    collector.setResult(0, taggedImage(250, 300));                        // valid

    QVERIFY(done);
    // Product kept; only the two valid images survive, in URL order 0 then 2.
    QCOMPARE(received.size(), 2);
    QCOMPARE(received.at(0)->width(), 250);
    QCOMPARE(received.at(1)->width(), 260);
}

// The dropped small image must be surfaced visibly (qWarning) with its URL —
// it is a filtered omission, never a silent drop (§XV-1). The product's valid
// images are still delivered.
void Test_Aspire_OrderedImageCollector::test_collector_min_size_drop_is_surfaced_with_url()
{
    QStringList warnings;
    s_capturedWarnings = &warnings;
    QtMessageHandler previous = qInstallMessageHandler(capturingMessageHandler);

    const QString smallUrl = QStringLiteral("https://cdn.example.com/thumbnail-48px.png");
    const QStringList urls{QStringLiteral("https://cdn.example.com/hero.jpg"), smallUrl};

    QList<QSharedPointer<QImage>> received;
    {
        OrderedImageCollector collector(
            urls.size(),
            [&](QList<QSharedPointer<QImage>> images) { received = images; },
            PageAttributesProduct::MIN_IMAGE_SIDE_PX,
            urls,
            QStringLiteral("https://shop.example.com/product/7"));
        collector.setResult(0, taggedImage(400, 400)); // valid
        collector.setResult(1, taggedImage(48, 400));  // 48 < 200 → dropped
    }

    qInstallMessageHandler(previous);
    s_capturedWarnings = nullptr;

    bool surfaced = false;
    for (const QString &w : std::as_const(warnings)) {
        if (w.contains(smallUrl)) {
            surfaced = true;
            break;
        }
    }
    QVERIFY2(surfaced, "dropped small-image URL was not surfaced in a qWarning");

    // The valid image is still recorded — the product is not dropped.
    QCOMPARE(received.size(), 1);
    QCOMPARE(received.at(0)->width(), 400);
}

// If EVERY image is below the threshold, the product ends up with no valid image:
// the callback receives an empty list (caller/validator then skips it) and an
// explicit "no valid image" reason is logged — not a silent drop.
void Test_Aspire_OrderedImageCollector::test_collector_all_below_min_size_yield_empty_list_with_reason()
{
    QStringList warnings;
    s_capturedWarnings = &warnings;
    QtMessageHandler previous = qInstallMessageHandler(capturingMessageHandler);

    const QStringList urls{
        QStringLiteral("https://cdn.example.com/tiny1.jpg"),
        QStringLiteral("https://cdn.example.com/tiny2.jpg")};

    QList<QSharedPointer<QImage>> received;
    bool done = false;
    {
        OrderedImageCollector collector(
            urls.size(),
            [&](QList<QSharedPointer<QImage>> images) {
                received = images;
                done = true;
            },
            PageAttributesProduct::MIN_IMAGE_SIDE_PX,
            urls,
            QStringLiteral("https://shop.example.com/product/99"));
        collector.setResult(0, taggedImage(199, 300)); // 199 < 200 → dropped
        collector.setResult(1, taggedImage(150, 150)); // 150 < 200 → dropped
    }

    qInstallMessageHandler(previous);
    s_capturedWarnings = nullptr;

    QVERIFY(done);
    QVERIFY(received.isEmpty());

    bool reasonLogged = false;
    for (const QString &w : std::as_const(warnings)) {
        if (w.contains(QStringLiteral("NO image")) || w.contains(QStringLiteral("no valid image"))) {
            reasonLogged = true;
            break;
        }
    }
    QVERIFY2(reasonLogged, "no-valid-image skip reason was not surfaced in a qWarning");
}

QTEST_MAIN(Test_Aspire_OrderedImageCollector)
#include "test_ordered_image_collector.moc"
