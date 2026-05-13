#include <QtTest>

#include "website/social/AbstractSocialMedia.h"
#include "website/social/SocialMediaGoogle.h"
#include "website/social/SocialMediaLinkedIn.h"
#include "website/social/SocialMediaOpenGraph.h"
#include "website/social/SocialMediaPinterest.h"
#include "website/social/SocialMediaTwitter.h"
#include "website/social/SocialMediaTwitterSummary.h"

class Test_Website_SocialMedia : public QObject
{
    Q_OBJECT

private slots:
    // --- AbstractSocialMedia helpers ---
    void test_socialmedia_landscape_dimensions();
    void test_socialmedia_wide_dimensions();
    void test_socialmedia_square_dimensions();
    void test_socialmedia_portrait_dimensions();
    void test_socialmedia_svg_suffix_landscape();
    void test_socialmedia_svg_suffix_wide();
    void test_socialmedia_svg_suffix_square();
    void test_socialmedia_svg_suffix_portrait();
    void test_socialmedia_webp_suffix_landscape();
    void test_socialmedia_webp_suffix_portrait();

    // --- registry ---
    void test_socialmedia_registry_contains_opengraph();
    void test_socialmedia_registry_contains_linkedin();
    void test_socialmedia_registry_contains_twitter();
    void test_socialmedia_registry_contains_twitter_summary();
    void test_socialmedia_registry_contains_google();
    void test_socialmedia_registry_contains_pinterest();
    void test_socialmedia_registry_all_have_unique_ids();

    // --- SocialMediaOpenGraph ---
    void test_og_id();
    void test_og_required_size_is_landscape();
    void test_og_image_tag_contains_url();
    void test_og_image_tag_contains_width();
    void test_og_image_tag_contains_height();
    void test_og_title_tag_contains_text();
    void test_og_desc_tag_contains_text();
    void test_og_title_empty_returns_empty();
    void test_og_desc_empty_returns_empty();

    // --- SocialMediaTwitter ---
    void test_twitter_id();
    void test_twitter_required_size_is_landscape();
    void test_twitter_image_tag_contains_summary_large_image();
    void test_twitter_image_tag_contains_url();
    void test_twitter_title_tag_contains_text();

    // --- SocialMediaTwitterSummary ---
    void test_twitter_summary_id();
    void test_twitter_summary_required_size_is_square();
    void test_twitter_summary_image_tag_contains_summary();

    // --- SocialMediaGoogle ---
    void test_google_id();
    void test_google_required_size_is_wide();
    void test_google_image_tag_contains_itemprop();
    void test_google_image_tag_contains_json_ld();
    void test_google_title_returns_empty();

    // --- SocialMediaPinterest ---
    void test_pinterest_id();
    void test_pinterest_required_size_is_portrait();
    void test_pinterest_image_tag_contains_url();
    void test_pinterest_image_tag_contains_portrait_dimensions();

    // --- SocialMediaLinkedIn ---
    void test_linkedin_id();
    void test_linkedin_required_size_is_landscape();
    void test_linkedin_image_tag_contains_url();
    void test_linkedin_image_tag_contains_dimensions();
};

// =============================================================================
// AbstractSocialMedia helpers
// =============================================================================

void Test_Website_SocialMedia::test_socialmedia_landscape_dimensions()
{
    const QSize s = AbstractSocialMedia::imageSizeDimensions(AbstractSocialMedia::ImageSize::Landscape);
    QCOMPARE(s.width(),  1200);
    QCOMPARE(s.height(),  630);
}

void Test_Website_SocialMedia::test_socialmedia_wide_dimensions()
{
    const QSize s = AbstractSocialMedia::imageSizeDimensions(AbstractSocialMedia::ImageSize::Wide);
    QCOMPARE(s.width(),  1200);
    QCOMPARE(s.height(),  675);
}

void Test_Website_SocialMedia::test_socialmedia_square_dimensions()
{
    const QSize s = AbstractSocialMedia::imageSizeDimensions(AbstractSocialMedia::ImageSize::Square);
    QCOMPARE(s.width(),  1200);
    QCOMPARE(s.height(), 1200);
}

void Test_Website_SocialMedia::test_socialmedia_portrait_dimensions()
{
    const QSize s = AbstractSocialMedia::imageSizeDimensions(AbstractSocialMedia::ImageSize::Portrait);
    QCOMPARE(s.width(),  1000);
    QCOMPARE(s.height(), 1500);
}

void Test_Website_SocialMedia::test_socialmedia_svg_suffix_landscape()
{
    QCOMPARE(AbstractSocialMedia::svgSuffix(AbstractSocialMedia::ImageSize::Landscape),
             QStringLiteral("-og.svg"));
}

void Test_Website_SocialMedia::test_socialmedia_svg_suffix_wide()
{
    QCOMPARE(AbstractSocialMedia::svgSuffix(AbstractSocialMedia::ImageSize::Wide),
             QStringLiteral("-wide.svg"));
}

void Test_Website_SocialMedia::test_socialmedia_svg_suffix_square()
{
    QCOMPARE(AbstractSocialMedia::svgSuffix(AbstractSocialMedia::ImageSize::Square),
             QStringLiteral("-square.svg"));
}

void Test_Website_SocialMedia::test_socialmedia_svg_suffix_portrait()
{
    QCOMPARE(AbstractSocialMedia::svgSuffix(AbstractSocialMedia::ImageSize::Portrait),
             QStringLiteral("-portrait.svg"));
}

void Test_Website_SocialMedia::test_socialmedia_webp_suffix_landscape()
{
    QCOMPARE(AbstractSocialMedia::webpSuffix(AbstractSocialMedia::ImageSize::Landscape),
             QStringLiteral("-og.webp"));
}

void Test_Website_SocialMedia::test_socialmedia_webp_suffix_portrait()
{
    QCOMPARE(AbstractSocialMedia::webpSuffix(AbstractSocialMedia::ImageSize::Portrait),
             QStringLiteral("-portrait.webp"));
}

// =============================================================================
// Registry
// =============================================================================

static AbstractSocialMedia *findById(const QString &id)
{
    for (AbstractSocialMedia *p : AbstractSocialMedia::all()) {
        if (p->getId() == id) {
            return p;
        }
    }
    return nullptr;
}

void Test_Website_SocialMedia::test_socialmedia_registry_contains_opengraph()
{
    QVERIFY(findById(QStringLiteral("opengraph")) != nullptr);
}

void Test_Website_SocialMedia::test_socialmedia_registry_contains_linkedin()
{
    QVERIFY(findById(QStringLiteral("linkedin")) != nullptr);
}

void Test_Website_SocialMedia::test_socialmedia_registry_contains_twitter()
{
    QVERIFY(findById(QStringLiteral("twitter")) != nullptr);
}

void Test_Website_SocialMedia::test_socialmedia_registry_contains_twitter_summary()
{
    QVERIFY(findById(QStringLiteral("twitter_summary")) != nullptr);
}

void Test_Website_SocialMedia::test_socialmedia_registry_contains_google()
{
    QVERIFY(findById(QStringLiteral("google")) != nullptr);
}

void Test_Website_SocialMedia::test_socialmedia_registry_contains_pinterest()
{
    QVERIFY(findById(QStringLiteral("pinterest")) != nullptr);
}

void Test_Website_SocialMedia::test_socialmedia_registry_all_have_unique_ids()
{
    QSet<QString> ids;
    for (AbstractSocialMedia *p : AbstractSocialMedia::all()) {
        QVERIFY2(!ids.contains(p->getId()),
                 qPrintable(QStringLiteral("Duplicate id: ") + p->getId()));
        ids.insert(p->getId());
    }
}

// =============================================================================
// SocialMediaOpenGraph
// =============================================================================

void Test_Website_SocialMedia::test_og_id()
{
    SocialMediaOpenGraph og;
    QCOMPARE(og.getId(), QStringLiteral("opengraph"));
}

void Test_Website_SocialMedia::test_og_required_size_is_landscape()
{
    SocialMediaOpenGraph og;
    QCOMPARE(og.requiredImageSize(), AbstractSocialMedia::ImageSize::Landscape);
}

void Test_Website_SocialMedia::test_og_image_tag_contains_url()
{
    SocialMediaOpenGraph og;
    const QString &html = og.imageMetaTagHtml(QStringLiteral("https://ex.com/img-og.webp"));
    QVERIFY(html.contains(QStringLiteral("https://ex.com/img-og.webp")));
}

void Test_Website_SocialMedia::test_og_image_tag_contains_width()
{
    SocialMediaOpenGraph og;
    const QString &html = og.imageMetaTagHtml(QStringLiteral("https://ex.com/img-og.webp"));
    QVERIFY(html.contains(QStringLiteral("og:image:width")));
    QVERIFY(html.contains(QStringLiteral("1200")));
}

void Test_Website_SocialMedia::test_og_image_tag_contains_height()
{
    SocialMediaOpenGraph og;
    const QString &html = og.imageMetaTagHtml(QStringLiteral("https://ex.com/img-og.webp"));
    QVERIFY(html.contains(QStringLiteral("og:image:height")));
    QVERIFY(html.contains(QStringLiteral("630")));
}

void Test_Website_SocialMedia::test_og_title_tag_contains_text()
{
    SocialMediaOpenGraph og;
    const QString &html = og.titleMetaTagHtml(QStringLiteral("My Article"));
    QVERIFY(html.contains(QStringLiteral("og:title")));
    QVERIFY(html.contains(QStringLiteral("My Article")));
}

void Test_Website_SocialMedia::test_og_desc_tag_contains_text()
{
    SocialMediaOpenGraph og;
    const QString &html = og.descMetaTagHtml(QStringLiteral("A description."));
    QVERIFY(html.contains(QStringLiteral("og:description")));
    QVERIFY(html.contains(QStringLiteral("A description.")));
}

void Test_Website_SocialMedia::test_og_title_empty_returns_empty()
{
    SocialMediaOpenGraph og;
    QVERIFY(og.titleMetaTagHtml(QString()).isEmpty());
}

void Test_Website_SocialMedia::test_og_desc_empty_returns_empty()
{
    SocialMediaOpenGraph og;
    QVERIFY(og.descMetaTagHtml(QString()).isEmpty());
}

// =============================================================================
// SocialMediaTwitter
// =============================================================================

void Test_Website_SocialMedia::test_twitter_id()
{
    SocialMediaTwitter tw;
    QCOMPARE(tw.getId(), QStringLiteral("twitter"));
}

void Test_Website_SocialMedia::test_twitter_required_size_is_landscape()
{
    SocialMediaTwitter tw;
    QCOMPARE(tw.requiredImageSize(), AbstractSocialMedia::ImageSize::Landscape);
}

void Test_Website_SocialMedia::test_twitter_image_tag_contains_summary_large_image()
{
    SocialMediaTwitter tw;
    const QString &html = tw.imageMetaTagHtml(QStringLiteral("https://ex.com/img-og.webp"));
    QVERIFY(html.contains(QStringLiteral("summary_large_image")));
}

void Test_Website_SocialMedia::test_twitter_image_tag_contains_url()
{
    SocialMediaTwitter tw;
    const QString &html = tw.imageMetaTagHtml(QStringLiteral("https://ex.com/img-og.webp"));
    QVERIFY(html.contains(QStringLiteral("https://ex.com/img-og.webp")));
}

void Test_Website_SocialMedia::test_twitter_title_tag_contains_text()
{
    SocialMediaTwitter tw;
    const QString &html = tw.titleMetaTagHtml(QStringLiteral("My Title"));
    QVERIFY(html.contains(QStringLiteral("twitter:title")));
    QVERIFY(html.contains(QStringLiteral("My Title")));
}

// =============================================================================
// SocialMediaTwitterSummary
// =============================================================================

void Test_Website_SocialMedia::test_twitter_summary_id()
{
    SocialMediaTwitterSummary ts;
    QCOMPARE(ts.getId(), QStringLiteral("twitter_summary"));
}

void Test_Website_SocialMedia::test_twitter_summary_required_size_is_square()
{
    SocialMediaTwitterSummary ts;
    QCOMPARE(ts.requiredImageSize(), AbstractSocialMedia::ImageSize::Square);
}

void Test_Website_SocialMedia::test_twitter_summary_image_tag_contains_summary()
{
    SocialMediaTwitterSummary ts;
    const QString &html = ts.imageMetaTagHtml(QStringLiteral("https://ex.com/img-square.webp"));
    QVERIFY(html.contains(QStringLiteral("\"summary\"")));
}

// =============================================================================
// SocialMediaGoogle
// =============================================================================

void Test_Website_SocialMedia::test_google_id()
{
    SocialMediaGoogle g;
    QCOMPARE(g.getId(), QStringLiteral("google"));
}

void Test_Website_SocialMedia::test_google_required_size_is_wide()
{
    SocialMediaGoogle g;
    QCOMPARE(g.requiredImageSize(), AbstractSocialMedia::ImageSize::Wide);
}

void Test_Website_SocialMedia::test_google_image_tag_contains_itemprop()
{
    SocialMediaGoogle g;
    const QString &html = g.imageMetaTagHtml(QStringLiteral("https://ex.com/img-wide.webp"));
    QVERIFY(html.contains(QStringLiteral("itemprop=\"image\"")));
}

void Test_Website_SocialMedia::test_google_image_tag_contains_json_ld()
{
    SocialMediaGoogle g;
    const QString &html = g.imageMetaTagHtml(QStringLiteral("https://ex.com/img-wide.webp"));
    QVERIFY(html.contains(QStringLiteral("application/ld+json")));
    QVERIFY(html.contains(QStringLiteral("ImageObject")));
}

void Test_Website_SocialMedia::test_google_title_returns_empty()
{
    SocialMediaGoogle g;
    QVERIFY(g.titleMetaTagHtml(QStringLiteral("anything")).isEmpty());
}

// =============================================================================
// SocialMediaPinterest
// =============================================================================

void Test_Website_SocialMedia::test_pinterest_id()
{
    SocialMediaPinterest pt;
    QCOMPARE(pt.getId(), QStringLiteral("pinterest"));
}

void Test_Website_SocialMedia::test_pinterest_required_size_is_portrait()
{
    SocialMediaPinterest pt;
    QCOMPARE(pt.requiredImageSize(), AbstractSocialMedia::ImageSize::Portrait);
}

void Test_Website_SocialMedia::test_pinterest_image_tag_contains_url()
{
    SocialMediaPinterest pt;
    const QString &html = pt.imageMetaTagHtml(QStringLiteral("https://ex.com/img-portrait.webp"));
    QVERIFY(html.contains(QStringLiteral("https://ex.com/img-portrait.webp")));
}

void Test_Website_SocialMedia::test_pinterest_image_tag_contains_portrait_dimensions()
{
    SocialMediaPinterest pt;
    const QString &html = pt.imageMetaTagHtml(QStringLiteral("https://ex.com/img-portrait.webp"));
    QVERIFY(html.contains(QStringLiteral("1000")));
    QVERIFY(html.contains(QStringLiteral("1500")));
}

// =============================================================================
// SocialMediaLinkedIn
// =============================================================================

void Test_Website_SocialMedia::test_linkedin_id()
{
    SocialMediaLinkedIn li;
    QCOMPARE(li.getId(), QStringLiteral("linkedin"));
}

void Test_Website_SocialMedia::test_linkedin_required_size_is_landscape()
{
    SocialMediaLinkedIn li;
    QCOMPARE(li.requiredImageSize(), AbstractSocialMedia::ImageSize::Landscape);
}

void Test_Website_SocialMedia::test_linkedin_image_tag_contains_url()
{
    SocialMediaLinkedIn li;
    const QString &html = li.imageMetaTagHtml(QStringLiteral("https://ex.com/img-og.webp"));
    QVERIFY(html.contains(QStringLiteral("https://ex.com/img-og.webp")));
}

void Test_Website_SocialMedia::test_linkedin_image_tag_contains_dimensions()
{
    SocialMediaLinkedIn li;
    const QString &html = li.imageMetaTagHtml(QStringLiteral("https://ex.com/img-og.webp"));
    QVERIFY(html.contains(QStringLiteral("og:image:width")));
    QVERIFY(html.contains(QStringLiteral("og:image:height")));
}

QTEST_MAIN(Test_Website_SocialMedia)
#include "test_social_media.moc"
