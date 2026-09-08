#include <QtTest>

#include <QTemporaryDir>

#include "aicli/AbstractCli.h"
#include "website/EngineLanguages.h"
#include "website/pages/GenPageQueue.h"
#include "website/pages/PageRecord.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/shortcodes/AbstractShortCodeImage.h"

class Test_Website_GenPageQueueRaster : public QObject
{
    Q_OBJECT

private slots:
    void test_genpagequeue_wants_raster_image_false_when_instructions_empty();
    void test_genpagequeue_wants_raster_image_true_when_instructions_set();
    void test_genpagequeue_content_prompt_omits_raster_block_by_default();
    void test_genpagequeue_content_prompt_includes_instructions_when_enabled();
    void test_genpagequeue_content_prompt_states_count_range_when_set();
    void test_genpagequeue_content_prompt_omits_count_range_when_unset();
    void test_genpagequeue_raster_prompt_contains_marker();
    void test_genpagequeue_raster_prompt_contains_alt_and_output_path();
    void test_genpagequeue_raster_prompt_contains_style_instructions();
    void test_genpagequeue_svg_prompt_never_contains_raster_marker();
    void test_genpagequeue_extract_section_scopes_to_bounding_headings();
    void test_genpagequeue_extract_section_open_ended_for_last_heading();
    void test_genpagequeue_extract_section_falls_back_when_id_not_found();
    void test_genpagequeue_extract_section_falls_back_when_no_preceding_heading();
    void test_genpagequeue_extract_section_falls_back_when_id_empty();
    void test_genpagequeue_raster_prompt_excludes_other_sections();
    void test_genpagequeue_parseimgfix_normalizes_path_prefixed_filename();
    void test_genpagequeue_parseimgfix_leaves_bare_filename_untouched();
    void test_genpagequeue_normalized_filename_variants_all_agree();
};

namespace {

// GenPageQueue's virtualPages constructor needs a CategoryTable bound to some
// working directory; buildContentPrompt()/buildRasterImagePrompt() don't touch
// it, so an empty temp dir is sufficient.
struct Fixture {
    QTemporaryDir dir;
    CategoryTable categoryTable;

    Fixture() : categoryTable(QDir(dir.path())) { }
};

PageRecord makePage()
{
    PageRecord page;
    page.permalink = QStringLiteral("/burgundy-dress-shoes");
    page.lang      = QStringLiteral("en");
    return page;
}

} // namespace

void Test_Website_GenPageQueueRaster::test_genpagequeue_wants_raster_image_false_when_instructions_empty()
{
    Fixture f;
    GenPageQueue queue(QStringLiteral("article"), false, QList<PageRecord>{}, f.categoryTable);

    QVERIFY(!queue.wantsRasterImage());
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_wants_raster_image_true_when_instructions_set()
{
    Fixture f;
    GenPageQueue queue(QStringLiteral("article"), false, QList<PageRecord>{}, f.categoryTable,
                       QString{}, QString{}, QDir{},
                       QStringLiteral("Photorealistic fashion photography."));

    QVERIFY(queue.wantsRasterImage());
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_content_prompt_omits_raster_block_by_default()
{
    Fixture f;
    GenPageQueue queue(QStringLiteral("article"), false, QList<PageRecord>{}, f.categoryTable);
    EngineLanguages engine;

    const QString prompt = queue.buildContentPrompt(makePage(), engine, 0);

    QVERIFY(!prompt.contains(QStringLiteral("Raster image generation is enabled")));
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_content_prompt_includes_instructions_when_enabled()
{
    Fixture f;
    GenPageQueue queue(QStringLiteral("article"), false, QList<PageRecord>{}, f.categoryTable,
                       QString{}, QString{}, QDir{},
                       QStringLiteral("LAYR & LAYR-inspired numbered outfit rules."));
    EngineLanguages engine;

    const QString prompt = queue.buildContentPrompt(makePage(), engine, 0);

    QVERIFY(prompt.contains(QStringLiteral("Raster image generation is enabled")));
    QVERIFY(prompt.contains(QStringLiteral("LAYR & LAYR-inspired numbered outfit rules.")));
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_content_prompt_states_count_range_when_set()
{
    Fixture f;
    GenPageQueue queue(QStringLiteral("article"), false, QList<PageRecord>{}, f.categoryTable,
                       QString{}, QString{}, QDir{},
                       QStringLiteral("Photorealistic style."), 8, 12);
    EngineLanguages engine;

    const QString prompt = queue.buildContentPrompt(makePage(), engine, 0);

    QVERIFY(prompt.contains(QStringLiteral("between 8 and 12")));
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_content_prompt_omits_count_range_when_unset()
{
    Fixture f;
    // Instructions set but count left at default (0/0) — must not state a range.
    GenPageQueue queue(QStringLiteral("article"), false, QList<PageRecord>{}, f.categoryTable,
                       QString{}, QString{}, QDir{},
                       QStringLiteral("Photorealistic style."));
    EngineLanguages engine;

    const QString prompt = queue.buildContentPrompt(makePage(), engine, 0);

    QVERIFY(!prompt.contains(QStringLiteral("between 0 and 0")));
    QVERIFY(!prompt.contains(QStringLiteral("Include between")));
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_raster_prompt_contains_marker()
{
    Fixture f;
    GenPageQueue queue(QStringLiteral("article"), false, QList<PageRecord>{}, f.categoryTable);
    GenPageQueue::ImgFixRef ref;
    ref.id       = QStringLiteral("idea-1");
    ref.fileName = QStringLiteral("burgundy-dress-heels.jpg");
    ref.alt      = QStringLiteral("Burgundy wrap dress with nude pointed-toe heels");

    const QString prompt = queue.buildRasterImagePrompt(
        ref, QStringLiteral("article text"), QStringLiteral("en"),
        QStringLiteral("/tmp/out/idea-1.jpg"));

    QVERIFY(prompt.contains(QString::fromLatin1(RASTER_IMAGE_PROMPT_MARKER)));
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_raster_prompt_contains_alt_and_output_path()
{
    Fixture f;
    GenPageQueue queue(QStringLiteral("article"), false, QList<PageRecord>{}, f.categoryTable);
    GenPageQueue::ImgFixRef ref;
    ref.id       = QStringLiteral("idea-1");
    ref.fileName = QStringLiteral("burgundy-dress-heels.jpg");
    ref.alt      = QStringLiteral("Burgundy wrap dress with nude pointed-toe heels");

    const QString prompt = queue.buildRasterImagePrompt(
        ref, QString{}, QStringLiteral("en"), QStringLiteral("/tmp/out/idea-1.jpg"));

    QVERIFY(prompt.contains(QStringLiteral("Burgundy wrap dress with nude pointed-toe heels")));
    QVERIFY(prompt.contains(QStringLiteral("/tmp/out/idea-1.jpg")));
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_raster_prompt_contains_style_instructions()
{
    Fixture f;
    GenPageQueue queue(QStringLiteral("article"), false, QList<PageRecord>{}, f.categoryTable,
                       QString{}, QString{}, QDir{},
                       QStringLiteral("Photorealistic, natural lighting, LAYR & LAYR voice."));
    GenPageQueue::ImgFixRef ref;
    ref.id  = QStringLiteral("idea-1");
    ref.alt = QStringLiteral("test");

    const QString prompt = queue.buildRasterImagePrompt(
        ref, QString{}, QStringLiteral("en"), QStringLiteral("/tmp/out/idea-1.jpg"));

    QVERIFY(prompt.contains(QStringLiteral("Photorealistic, natural lighting, LAYR & LAYR voice.")));
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_svg_prompt_never_contains_raster_marker()
{
    Fixture f;
    GenPageQueue queue(QStringLiteral("article"), false, QList<PageRecord>{}, f.categoryTable);
    GenPageQueue::ImgFixRef ref;
    ref.id       = QStringLiteral("hero");
    ref.fileName = QStringLiteral("hero.svg");
    ref.alt      = QStringLiteral("test");

    const QString prompt = queue.buildSvgPrompt(ref, QString{}, QStringLiteral("en"));

    QVERIFY(!prompt.contains(QString::fromLatin1(RASTER_IMAGE_PROMPT_MARKER)));
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_extract_section_scopes_to_bounding_headings()
{
    const QString article =
        QStringLiteral("[TITLE level=\"2\"]Type A[/TITLE]\n"
                       "Type A teaching text.\n"
                       "[IMGFIX id=\"a-1\" fileName=\"a1.jpg\" alt=\"A idea 1\"][/IMGFIX]\n"
                       "[IMGFIX id=\"a-2\" fileName=\"a2.jpg\" alt=\"A idea 2\"][/IMGFIX]\n"
                       "[TITLE level=\"2\"]Type B[/TITLE]\n"
                       "Type B teaching text.\n"
                       "[IMGFIX id=\"b-1\" fileName=\"b1.jpg\" alt=\"B idea 1\"][/IMGFIX]\n"
                       "[TITLE level=\"2\"]Type C[/TITLE]\n"
                       "Type C teaching text.\n");

    const QString sectionA = GenPageQueue::extractRelevantSection(article, QStringLiteral("a-1"));
    QVERIFY(sectionA.contains(QStringLiteral("Type A teaching text")));
    QVERIFY(sectionA.contains(QStringLiteral("a-2"))); // sibling image in the same section is fine
    QVERIFY(!sectionA.contains(QStringLiteral("Type B teaching text")));
    QVERIFY(!sectionA.contains(QStringLiteral("Type C teaching text")));

    const QString sectionB = GenPageQueue::extractRelevantSection(article, QStringLiteral("b-1"));
    QVERIFY(sectionB.contains(QStringLiteral("Type B teaching text")));
    QVERIFY(!sectionB.contains(QStringLiteral("Type A teaching text")));
    QVERIFY(!sectionB.contains(QStringLiteral("Type C teaching text")));
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_extract_section_open_ended_for_last_heading()
{
    const QString article =
        QStringLiteral("[TITLE level=\"2\"]Type A[/TITLE]\n"
                       "[TITLE level=\"2\"]Type B[/TITLE]\n"
                       "[IMGFIX id=\"b-1\" fileName=\"b1.jpg\" alt=\"B idea\"][/IMGFIX]\n"
                       "Rest of Type B content, no further heading follows.");

    const QString section = GenPageQueue::extractRelevantSection(article, QStringLiteral("b-1"));
    QVERIFY(section.contains(QStringLiteral("Rest of Type B content")));
    QVERIFY(!section.contains(QStringLiteral("Type A")));
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_extract_section_falls_back_when_id_not_found()
{
    const QString article =
        QStringLiteral("[TITLE level=\"2\"]Type A[/TITLE]\n"
                       "[IMGFIX id=\"a-1\" fileName=\"a1.jpg\" alt=\"A idea\"][/IMGFIX]\n");

    QCOMPARE(GenPageQueue::extractRelevantSection(article, QStringLiteral("does-not-exist")), article);
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_extract_section_falls_back_when_no_preceding_heading()
{
    // Image sits in the opening paragraph, before any [TITLE] heading.
    const QString article =
        QStringLiteral("Intro paragraph.\n"
                       "[IMGFIX id=\"intro-1\" fileName=\"i1.jpg\" alt=\"Intro idea\"][/IMGFIX]\n"
                       "[TITLE level=\"2\"]Type A[/TITLE]\n");

    QCOMPARE(GenPageQueue::extractRelevantSection(article, QStringLiteral("intro-1")), article);
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_extract_section_falls_back_when_id_empty()
{
    const QString article = QStringLiteral("[TITLE level=\"2\"]Type A[/TITLE]\ntext");
    QCOMPARE(GenPageQueue::extractRelevantSection(article, QString{}), article);
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_raster_prompt_excludes_other_sections()
{
    // End-to-end: buildRasterImagePrompt() must scope its "Article content"
    // block to the image's own section, not the whole article — proves the
    // wiring, not just the extraction helper in isolation.
    Fixture f;
    GenPageQueue queue(QStringLiteral("article"), false, QList<PageRecord>{}, f.categoryTable);

    const QString article =
        QStringLiteral("[TITLE level=\"2\"]Beige trousers with brown top[/TITLE]\n"
                       "Pair beige wide-leg trousers with a rich brown blouse.\n"
                       "[IMGFIX id=\"idea-1\" fileName=\"idea1.jpg\" alt=\"test\"][/IMGFIX]\n"
                       "[TITLE level=\"2\"]Navy dress with gold jewelry[/TITLE]\n"
                       "Style a navy slip dress with gold statement jewelry.\n");

    GenPageQueue::ImgFixRef ref;
    ref.id  = QStringLiteral("idea-1");
    ref.alt = QStringLiteral("test");

    const QString prompt = queue.buildRasterImagePrompt(
        ref, article, QStringLiteral("en"), QStringLiteral("/tmp/out/idea1.jpg"));

    QVERIFY(prompt.contains(QStringLiteral("beige wide-leg trousers")));
    QVERIFY(!prompt.contains(QStringLiteral("navy slip dress")));
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_parseimgfix_normalizes_path_prefixed_filename()
{
    // Regression (2026-09-07): the AI wrote fileName="/images/foo.jpg", the blob
    // was stored in images.db under that literal key while the rendered
    // <img src="//images/foo.jpg"> asked for the basename, so every image on the
    // page silently rendered broken even though generation reported success.
    const QString article =
        QStringLiteral("[IMGFIX id=\"a\" fileName=\"/images/tonal-navy.jpg\" alt=\"x\"][/IMGFIX]\n"
                       "[IMGFIX id=\"b\" fileName=\"images/crisp-white.jpg\" alt=\"y\"][/IMGFIX]");

    const QList<GenPageQueue::ImgFixRef> refs = GenPageQueue::parseImgFixRefs(article);
    QCOMPARE(refs.size(), 2);
    QCOMPARE(refs.at(0).fileName, QStringLiteral("tonal-navy.jpg"));
    QCOMPARE(refs.at(1).fileName, QStringLiteral("crisp-white.jpg"));
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_parseimgfix_leaves_bare_filename_untouched()
{
    const QString article =
        QStringLiteral("[IMGFIX id=\"a\" fileName=\"already-bare.jpg\" alt=\"x\"][/IMGFIX]");
    const QList<GenPageQueue::ImgFixRef> refs = GenPageQueue::parseImgFixRefs(article);
    QCOMPARE(refs.size(), 1);
    QCOMPARE(refs.at(0).fileName, QStringLiteral("already-bare.jpg"));
}

void Test_Website_GenPageQueueRaster::test_genpagequeue_normalized_filename_variants_all_agree()
{
    // Storage key and rendered src derive from this one function, so every
    // spelling the AI might emit must collapse to the identical basename.
    const QString expected = QStringLiteral("foo.jpg");
    QCOMPARE(AbstractShortCodeImage::normalizedFileName(QStringLiteral("foo.jpg")), expected);
    QCOMPARE(AbstractShortCodeImage::normalizedFileName(QStringLiteral("/foo.jpg")), expected);
    QCOMPARE(AbstractShortCodeImage::normalizedFileName(QStringLiteral("images/foo.jpg")), expected);
    QCOMPARE(AbstractShortCodeImage::normalizedFileName(QStringLiteral("/images/foo.jpg")), expected);
    QCOMPARE(AbstractShortCodeImage::normalizedFileName(QStringLiteral("  /images/foo.jpg  ")), expected);
    QCOMPARE(AbstractShortCodeImage::normalizedFileName(QStringLiteral("/a/b/c/foo.jpg")), expected);
}

QTEST_MAIN(Test_Website_GenPageQueueRaster)
#include "test_gen_page_queue_raster.moc"
