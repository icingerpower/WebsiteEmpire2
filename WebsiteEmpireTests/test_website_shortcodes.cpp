#include <QtTest>

#include "website/shortcodes/AbstractShortCode.h"
#include "website/shortcodes/ShortCodeVideo.h"
#include "website/shortcodes/ShortCodeLinkFix.h"
#include "website/shortcodes/ShortCodeLinkTr.h"
#include "website/shortcodes/ShortCodeSpinnable.h"
#include "website/shortcodes/ShortCodeImageFix.h"
#include "website/shortcodes/ShortCodeImageTr.h"
#include "website/shortcodes/ShortCodeTitle.h"
#include "ExceptionWithTitleText.h"

// =============================================================================
// Helpers
// =============================================================================

namespace {

// Calls fn(); returns true if it threw ExceptionWithTitleText, false otherwise.
template<typename Fn>
bool throwsShortCodeException(Fn &&fn)
{
    try {
        fn();
        return false;
    } catch (const ExceptionWithTitleText &) {
        return true;
    }
}

// Calls sc.addCode with the given shortcode text; returns the html output.
QString htmlFrom(const AbstractShortCode &sc, const QString &shortcodeText)
{
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(shortcodeText, html, css, js, cssDoneIds, jsDoneIds);
    return html;
}

} // namespace

// =============================================================================
// Test_Website_ShortCodes  (one test per shortcode + 20 verifications for VIDEO)
// =============================================================================

class Test_Website_ShortCodes : public QObject
{
    Q_OBJECT

private slots:
    // --- ShortCodeVideo: identity & argument contract ---
    void test_video_tag_name();
    void test_video_available_arguments_count();
    void test_video_url_argument_id();
    void test_video_url_argument_is_mandatory();
    void test_video_url_argument_has_no_allowed_values();
    void test_video_url_argument_not_translatable();

    // --- ShortCodeVideo: registry & factory ---
    void test_video_registered_in_all_shortcodes();
    void test_video_for_tag_returns_instance();

    // --- Registry (general) ---
    void test_registry_for_tag_unknown_returns_nullptr();

    // --- ShortCodeVideo: addCode output ---
    void test_video_add_code_double_quoted_url_in_html();
    void test_video_add_code_single_quoted_url_in_html();
    void test_video_add_code_unquoted_url_in_html();
    void test_video_add_code_appends_to_existing_html();
    void test_video_add_code_does_not_touch_css();
    void test_video_add_code_does_not_touch_js();
    void test_video_add_code_via_for_tag();

    // --- ShortCodeVideo: parse error cases ---
    void test_video_add_code_mismatched_tags_throws();
    void test_video_add_code_malformed_throws();
    void test_video_add_code_duplicate_argument_throws();

    // --- ShortCodeVideo: validation error cases ---
    void test_video_add_code_missing_url_throws();
    void test_video_add_code_empty_url_throws();
    void test_video_add_code_unknown_argument_throws();

    // --- ShortCodeLinkFix: identity & argument contract ---
    void test_linkfix_tag_name();
    void test_linkfix_available_arguments_count();
    void test_linkfix_url_argument_id();
    void test_linkfix_url_argument_is_mandatory();
    void test_linkfix_url_argument_not_translatable();
    void test_linkfix_rel_argument_id();
    void test_linkfix_rel_argument_not_mandatory();
    void test_linkfix_rel_argument_has_no_allowed_values();

    // --- ShortCodeLinkFix: registry & factory ---
    void test_linkfix_registered_in_all_shortcodes();
    void test_linkfix_for_tag_returns_instance();

    // --- ShortCodeLinkFix: addCode output ---
    void test_linkfix_add_code_url_in_html();
    void test_linkfix_add_code_inner_content_in_html();
    void test_linkfix_add_code_default_rel_dofollow();
    void test_linkfix_add_code_custom_rel();
    void test_linkfix_add_code_appends_to_existing_html();
    void test_linkfix_add_code_does_not_touch_css();
    void test_linkfix_add_code_does_not_touch_js();
    void test_linkfix_add_code_single_quoted_url();
    void test_linkfix_add_code_via_for_tag();

    // --- ShortCodeLinkFix: error cases ---
    void test_linkfix_add_code_mismatched_tags_throws();
    void test_linkfix_add_code_missing_url_throws();
    void test_linkfix_add_code_empty_url_throws();
    void test_linkfix_add_code_unknown_argument_throws();

    // --- ShortCodeLinkTr: identity & argument contract ---
    void test_linktr_tag_name();
    void test_linktr_available_arguments_count();
    void test_linktr_url_argument_is_mandatory();
    void test_linktr_url_argument_translatable_optional();
    void test_linktr_rel_argument_id();
    void test_linktr_rel_argument_not_mandatory();

    // --- ShortCodeLinkTr: registry & factory ---
    void test_linktr_registered_in_all_shortcodes();
    void test_linktr_for_tag_returns_instance();

    // --- ShortCodeLinkTr: addCode output ---
    void test_linktr_add_code_url_in_html();
    void test_linktr_add_code_inner_content_in_html();
    void test_linktr_add_code_default_rel_dofollow();
    void test_linktr_add_code_custom_rel_sponsored();
    void test_linktr_add_code_custom_rel_nofollow();
    void test_linktr_add_code_does_not_touch_css();
    void test_linktr_add_code_does_not_touch_js();
    void test_linktr_add_code_via_for_tag();

    // --- ShortCodeLinkTr: error cases ---
    void test_linktr_add_code_missing_url_throws();
    void test_linktr_add_code_empty_url_throws();
    void test_linktr_add_code_empty_rel_throws();

    // --- ShortCodeSpinnable: identity & argument contract ---
    void test_spinnable_tag_name();
    void test_spinnable_available_arguments_count();
    void test_spinnable_id_argument_id();
    void test_spinnable_id_argument_is_mandatory();
    void test_spinnable_id_argument_not_translatable();
    void test_spinnable_random_argument_id();
    void test_spinnable_random_argument_not_mandatory();
    void test_spinnable_random_argument_allowed_values();

    // --- ShortCodeSpinnable: registry & factory ---
    void test_spinnable_registered_in_all_shortcodes();
    void test_spinnable_for_tag_returns_instance();

    // --- ShortCodeSpinnable: addCode output ---
    void test_spinnable_add_code_result_is_one_of_options();
    void test_spinnable_add_code_fixed_seed_deterministic();
    void test_spinnable_add_code_result_has_no_braces();
    void test_spinnable_add_code_plain_text_preserved();
    void test_spinnable_add_code_nested_group();
    void test_spinnable_add_code_multiple_groups();
    void test_spinnable_add_code_appends_to_existing_html();
    void test_spinnable_add_code_does_not_touch_css();
    void test_spinnable_add_code_does_not_touch_js();
    void test_spinnable_add_code_shortcode_in_content_preserved();
    void test_spinnable_add_code_random_flag_output_valid();
    void test_spinnable_add_code_via_for_tag();

    // --- ShortCodeSpinnable: argument validation errors ---
    void test_spinnable_add_code_missing_id_throws();
    void test_spinnable_add_code_id_not_digits_throws();
    void test_spinnable_add_code_id_negative_throws();
    void test_spinnable_add_code_id_float_throws();
    void test_spinnable_add_code_invalid_random_value_throws();
    void test_spinnable_add_code_unknown_argument_throws();

    // --- ShortCodeImageFix: identity & argument contract ---
    void test_imgfix_tag_name();
    void test_imgfix_available_arguments_count();
    void test_imgfix_id_argument_id();
    void test_imgfix_id_argument_is_mandatory();
    void test_imgfix_id_argument_not_translatable();
    void test_imgfix_filename_argument_id();
    void test_imgfix_filename_argument_is_mandatory();
    void test_imgfix_filename_argument_translatable_optional();
    void test_imgfix_alt_argument_id();
    void test_imgfix_alt_argument_is_mandatory();
    void test_imgfix_alt_argument_translatable_yes();
    void test_imgfix_width_argument_not_mandatory();
    void test_imgfix_height_argument_not_mandatory();

    // --- ShortCodeImageFix: registry & factory ---
    void test_imgfix_registered_in_all_shortcodes();
    void test_imgfix_for_tag_returns_instance();

    // --- ShortCodeImageFix: addCode output ---
    void test_imgfix_add_code_alt_in_html();
    void test_imgfix_add_code_src_is_todo_webp();
    void test_imgfix_add_code_width_in_html();
    void test_imgfix_add_code_height_in_html();
    void test_imgfix_add_code_width_absent_not_in_html();
    void test_imgfix_add_code_height_absent_not_in_html();
    void test_imgfix_add_code_appends_to_existing_html();
    void test_imgfix_add_code_does_not_touch_css();
    void test_imgfix_add_code_does_not_touch_js();
    void test_imgfix_add_code_via_for_tag();

    // --- ShortCodeImageFix: error cases ---
    void test_imgfix_add_code_missing_id_throws();
    void test_imgfix_add_code_missing_filename_throws();
    void test_imgfix_add_code_missing_alt_throws();
    void test_imgfix_add_code_empty_alt_throws();
    void test_imgfix_add_code_invalid_width_throws();
    void test_imgfix_add_code_invalid_height_throws();
    void test_imgfix_add_code_unknown_argument_throws();
    void test_imgfix_add_code_mismatched_tags_throws();

    // --- ShortCodeImageTr: identity & argument contract ---
    void test_imgtr_tag_name();
    void test_imgtr_available_arguments_count();
    void test_imgtr_id_argument_translatable_yes();
    void test_imgtr_filename_argument_translatable_yes();
    void test_imgtr_alt_argument_translatable_yes();
    void test_imgtr_width_argument_not_mandatory();
    void test_imgtr_height_argument_not_mandatory();

    // --- ShortCodeImageTr: registry & factory ---
    void test_imgtr_registered_in_all_shortcodes();
    void test_imgtr_for_tag_returns_instance();

    // --- ShortCodeImageTr: addCode output ---
    void test_imgtr_add_code_alt_in_html();
    void test_imgtr_add_code_src_is_todo_webp();
    void test_imgtr_add_code_with_width_and_height();
    void test_imgtr_add_code_width_absent_not_in_html();
    void test_imgtr_add_code_appends_to_existing_html();
    void test_imgtr_add_code_does_not_touch_css();
    void test_imgtr_add_code_does_not_touch_js();
    void test_imgtr_add_code_via_for_tag();

    // --- ShortCodeImageTr: error cases ---
    void test_imgtr_add_code_missing_id_throws();
    void test_imgtr_add_code_missing_filename_throws();
    void test_imgtr_add_code_missing_alt_throws();
    void test_imgtr_add_code_empty_alt_throws();
    void test_imgtr_add_code_invalid_width_throws();
    void test_imgtr_add_code_invalid_height_throws();
    void test_imgtr_add_code_unknown_argument_throws();
    void test_imgtr_add_code_mismatched_tags_throws();

    // --- ShortCodeSpinnable: spinning syntax errors ---
    void test_spinnable_add_code_no_spin_group_throws();
    void test_spinnable_add_code_unmatched_open_brace_throws();
    void test_spinnable_add_code_unmatched_close_brace_throws();
    void test_spinnable_add_code_group_no_pipe_throws();
    void test_spinnable_add_code_nested_unmatched_brace_throws();
    void test_spinnable_add_code_malformed_shortcode_throws();
    void test_spinnable_add_code_mismatched_tags_throws();

    // --- ShortCodeTitle: identity & argument contract ---
    void test_title_tag_name();
    void test_title_available_arguments_count();
    void test_title_level_argument_id();
    void test_title_level_argument_is_mandatory();
    void test_title_level_argument_has_six_allowed_values();
    void test_title_level_argument_not_translatable();

    // --- ShortCodeTitle: registry & factory ---
    void test_title_registered_in_all_shortcodes();
    void test_title_for_tag_returns_instance();

    // --- ShortCodeTitle: addCode output ---
    void test_title_add_code_h1_tag_in_html();
    void test_title_add_code_h3_tag_in_html();
    void test_title_add_code_h6_tag_in_html();
    void test_title_add_code_inner_content_in_html();
    void test_title_add_code_appends_to_existing_html();
    void test_title_add_code_does_not_touch_css();
    void test_title_add_code_does_not_touch_js();
    void test_title_add_code_via_for_tag();

    // --- ShortCodeTitle: validation error cases ---
    void test_title_add_code_missing_level_throws();
    void test_title_add_code_invalid_level_zero_throws();
    void test_title_add_code_invalid_level_seven_throws();
    void test_title_add_code_invalid_level_alpha_throws();
    void test_title_add_code_unknown_argument_throws();
    void test_title_add_code_mismatched_tags_throws();
    void test_title_add_code_duplicate_argument_throws();
};

// =============================================================================
// ShortCodeVideo — identity & argument contract
// =============================================================================

void Test_Website_ShortCodes::test_video_tag_name()
{
    ShortCodeVideo sc;
    QCOMPARE(sc.getTag(), QStringLiteral("VIDEO"));
}

void Test_Website_ShortCodes::test_video_available_arguments_count()
{
    ShortCodeVideo sc;
    QCOMPARE(sc.availableArguments().size(), 1);
}

void Test_Website_ShortCodes::test_video_url_argument_id()
{
    ShortCodeVideo sc;
    const auto &args = sc.availableArguments();
    QVERIFY(!args.isEmpty());
    QCOMPARE(args.first().id, QStringLiteral("url"));
}

void Test_Website_ShortCodes::test_video_url_argument_is_mandatory()
{
    ShortCodeVideo sc;
    const auto &args = sc.availableArguments();
    QVERIFY(!args.isEmpty());
    QVERIFY(args.first().mandatory);
}

void Test_Website_ShortCodes::test_video_url_argument_has_no_allowed_values()
{
    ShortCodeVideo sc;
    const auto &args = sc.availableArguments();
    QVERIFY(!args.isEmpty());
    QVERIFY(args.first().allowedValues.isEmpty());
}

void Test_Website_ShortCodes::test_video_url_argument_not_translatable()
{
    ShortCodeVideo sc;
    const auto &args = sc.availableArguments();
    QVERIFY(!args.isEmpty());
    QVERIFY(args.first().translatable == AbstractShortCode::Translatable::No);
}

// =============================================================================
// ShortCodeVideo — registry & factory
// =============================================================================

void Test_Website_ShortCodes::test_video_registered_in_all_shortcodes()
{
    const auto &all = AbstractShortCode::ALL_SHORTCODES();
    QVERIFY(all.contains(QStringLiteral("VIDEO")));
    QVERIFY(all.value(QStringLiteral("VIDEO")) != nullptr);
}

void Test_Website_ShortCodes::test_video_for_tag_returns_instance()
{
    const AbstractShortCode *sc = AbstractShortCode::forTag(u"VIDEO");
    QVERIFY(sc != nullptr);
    QCOMPARE(sc->getTag(), QStringLiteral("VIDEO"));
}

void Test_Website_ShortCodes::test_registry_for_tag_unknown_returns_nullptr()
{
    QVERIFY(AbstractShortCode::forTag(u"NOSUCHSHORTCODE") == nullptr);
}

// =============================================================================
// ShortCodeVideo — addCode output
// =============================================================================

void Test_Website_ShortCodes::test_video_add_code_double_quoted_url_in_html()
{
    ShortCodeVideo sc;
    const QString input = QStringLiteral("[VIDEO url=\"https://youtu.be?v=abc\"][/VIDEO]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("https://youtu.be?v=abc")));
}

void Test_Website_ShortCodes::test_video_add_code_single_quoted_url_in_html()
{
    ShortCodeVideo sc;
    const QString input = QStringLiteral("[VIDEO url='https://youtu.be?v=abc'][/VIDEO]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("https://youtu.be?v=abc")));
}

void Test_Website_ShortCodes::test_video_add_code_unquoted_url_in_html()
{
    ShortCodeVideo sc;
    const QString input = QStringLiteral("[VIDEO url=https://youtu.be][/VIDEO]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("https://youtu.be")));
}

void Test_Website_ShortCodes::test_video_add_code_appends_to_existing_html()
{
    ShortCodeVideo sc;
    const QString prefix = QStringLiteral("<p>before</p>");
    QString html = prefix;
    QString css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[VIDEO url='https://youtu.be'][/VIDEO]"), html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.startsWith(prefix));
    QVERIFY(html.size() > prefix.size());
}

void Test_Website_ShortCodes::test_video_add_code_does_not_touch_css()
{
    ShortCodeVideo sc;
    QString html, js;
    QString css = QStringLiteral("existing-css");
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[VIDEO url='https://youtu.be'][/VIDEO]"), html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(css, QStringLiteral("existing-css"));
    QVERIFY(cssDoneIds.isEmpty());
}

void Test_Website_ShortCodes::test_video_add_code_does_not_touch_js()
{
    ShortCodeVideo sc;
    QString html, css;
    QString js = QStringLiteral("existing-js");
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[VIDEO url='https://youtu.be'][/VIDEO]"), html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(js, QStringLiteral("existing-js"));
    QVERIFY(jsDoneIds.isEmpty());
}

void Test_Website_ShortCodes::test_video_add_code_via_for_tag()
{
    // Exercises the full production path: registry lookup → addCode.
    const AbstractShortCode *sc = AbstractShortCode::forTag(u"VIDEO");
    QVERIFY(sc != nullptr);
    const QString input = QStringLiteral("[VIDEO url=\"https://youtu.be?v=xyz\"][/VIDEO]");
    const QString html = htmlFrom(*sc, input);
    QVERIFY(html.contains(QStringLiteral("https://youtu.be?v=xyz")));
}

// =============================================================================
// ShortCodeVideo — parse error cases
// =============================================================================

void Test_Website_ShortCodes::test_video_add_code_mismatched_tags_throws()
{
    ShortCodeVideo sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[VIDEO url=\"x\"][/IMG]"), html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_video_add_code_malformed_throws()
{
    ShortCodeVideo sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("this is not a shortcode"), html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_video_add_code_duplicate_argument_throws()
{
    ShortCodeVideo sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        const QString input = QStringLiteral("[VIDEO url=\"https://a.com\" url=\"https://b.com\"][/VIDEO]");
        sc.addCode(input, html, css, js, cssDoneIds, jsDoneIds);
    }));
}

// =============================================================================
// ShortCodeVideo — validation error cases
// =============================================================================

void Test_Website_ShortCodes::test_video_add_code_missing_url_throws()
{
    ShortCodeVideo sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[VIDEO][/VIDEO]"), html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_video_add_code_empty_url_throws()
{
    ShortCodeVideo sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[VIDEO url=\"\"][/VIDEO]"), html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_video_add_code_unknown_argument_throws()
{
    ShortCodeVideo sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        const QString input = QStringLiteral("[VIDEO url=\"https://youtu.be\" autoplay=1][/VIDEO]");
        sc.addCode(input, html, css, js, cssDoneIds, jsDoneIds);
    }));
}

// =============================================================================
// ShortCodeLinkFix — identity & argument contract
// =============================================================================

void Test_Website_ShortCodes::test_linkfix_tag_name()
{
    ShortCodeLinkFix sc;
    QCOMPARE(sc.getTag(), QStringLiteral("LINKFIX"));
}

void Test_Website_ShortCodes::test_linkfix_available_arguments_count()
{
    ShortCodeLinkFix sc;
    QCOMPARE(sc.availableArguments().size(), 2);
}

void Test_Website_ShortCodes::test_linkfix_url_argument_id()
{
    ShortCodeLinkFix sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 1);
    QCOMPARE(args.at(0).id, QStringLiteral("url"));
}

void Test_Website_ShortCodes::test_linkfix_url_argument_is_mandatory()
{
    ShortCodeLinkFix sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 1);
    QVERIFY(args.at(0).mandatory);
}

void Test_Website_ShortCodes::test_linkfix_url_argument_not_translatable()
{
    ShortCodeLinkFix sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 1);
    QVERIFY(args.at(0).translatable == AbstractShortCode::Translatable::No);
}

void Test_Website_ShortCodes::test_linkfix_rel_argument_id()
{
    ShortCodeLinkFix sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 2);
    QCOMPARE(args.at(1).id, QStringLiteral("rel"));
}

void Test_Website_ShortCodes::test_linkfix_rel_argument_not_mandatory()
{
    ShortCodeLinkFix sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 2);
    QVERIFY(!args.at(1).mandatory);
}

void Test_Website_ShortCodes::test_linkfix_rel_argument_has_no_allowed_values()
{
    ShortCodeLinkFix sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 2);
    QVERIFY(args.at(1).allowedValues.isEmpty());
}

// =============================================================================
// ShortCodeLinkFix — registry & factory
// =============================================================================

void Test_Website_ShortCodes::test_linkfix_registered_in_all_shortcodes()
{
    const auto &all = AbstractShortCode::ALL_SHORTCODES();
    QVERIFY(all.contains(QStringLiteral("LINKFIX")));
    QVERIFY(all.value(QStringLiteral("LINKFIX")) != nullptr);
}

void Test_Website_ShortCodes::test_linkfix_for_tag_returns_instance()
{
    const AbstractShortCode *sc = AbstractShortCode::forTag(u"LINKFIX");
    QVERIFY(sc != nullptr);
    QCOMPARE(sc->getTag(), QStringLiteral("LINKFIX"));
}

// =============================================================================
// ShortCodeLinkFix — addCode output
// =============================================================================

void Test_Website_ShortCodes::test_linkfix_add_code_url_in_html()
{
    ShortCodeLinkFix sc;
    const QString input = QStringLiteral("[LINKFIX url=\"https://example.com/study\"]Read it[/LINKFIX]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("https://example.com/study")));
}

void Test_Website_ShortCodes::test_linkfix_add_code_inner_content_in_html()
{
    ShortCodeLinkFix sc;
    const QString input = QStringLiteral("[LINKFIX url=\"https://example.com\"]middle text[/LINKFIX]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("middle text")));
}

void Test_Website_ShortCodes::test_linkfix_add_code_default_rel_dofollow()
{
    ShortCodeLinkFix sc;
    const QString input = QStringLiteral("[LINKFIX url=\"https://example.com\"]link[/LINKFIX]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("rel=\"dofollow\"")));
}

void Test_Website_ShortCodes::test_linkfix_add_code_custom_rel()
{
    ShortCodeLinkFix sc;
    const QString input = QStringLiteral("[LINKFIX url=\"https://example.com\" rel=\"nofollow\"]link[/LINKFIX]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("rel=\"nofollow\"")));
    QVERIFY(!html.contains(QStringLiteral("rel=\"dofollow\"")));
}

void Test_Website_ShortCodes::test_linkfix_add_code_appends_to_existing_html()
{
    ShortCodeLinkFix sc;
    const QString prefix = QStringLiteral("<p>before</p>");
    QString html = prefix;
    QString css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[LINKFIX url=\"https://example.com\"]link[/LINKFIX]"),
               html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.startsWith(prefix));
    QVERIFY(html.size() > prefix.size());
}

void Test_Website_ShortCodes::test_linkfix_add_code_does_not_touch_css()
{
    ShortCodeLinkFix sc;
    QString html, js;
    QString css = QStringLiteral("existing-css");
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[LINKFIX url=\"https://example.com\"]link[/LINKFIX]"),
               html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(css, QStringLiteral("existing-css"));
    QVERIFY(cssDoneIds.isEmpty());
}

void Test_Website_ShortCodes::test_linkfix_add_code_does_not_touch_js()
{
    ShortCodeLinkFix sc;
    QString html, css;
    QString js = QStringLiteral("existing-js");
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[LINKFIX url=\"https://example.com\"]link[/LINKFIX]"),
               html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(js, QStringLiteral("existing-js"));
    QVERIFY(jsDoneIds.isEmpty());
}

void Test_Website_ShortCodes::test_linkfix_add_code_single_quoted_url()
{
    ShortCodeLinkFix sc;
    const QString input = QStringLiteral("[LINKFIX url='https://example.com']link[/LINKFIX]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("https://example.com")));
}

void Test_Website_ShortCodes::test_linkfix_add_code_via_for_tag()
{
    const AbstractShortCode *sc = AbstractShortCode::forTag(u"LINKFIX");
    QVERIFY(sc != nullptr);
    const QString input = QStringLiteral("[LINKFIX url=\"https://example.com\"]via registry[/LINKFIX]");
    const QString html = htmlFrom(*sc, input);
    QVERIFY(html.contains(QStringLiteral("https://example.com")));
    QVERIFY(html.contains(QStringLiteral("via registry")));
}

// =============================================================================
// ShortCodeLinkFix — error cases
// =============================================================================

void Test_Website_ShortCodes::test_linkfix_add_code_mismatched_tags_throws()
{
    ShortCodeLinkFix sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[LINKFIX url=\"https://a.com\"]link[/VIDEO]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_linkfix_add_code_missing_url_throws()
{
    ShortCodeLinkFix sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[LINKFIX]link[/LINKFIX]"), html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_linkfix_add_code_empty_url_throws()
{
    ShortCodeLinkFix sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[LINKFIX url=\"\"]link[/LINKFIX]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_linkfix_add_code_unknown_argument_throws()
{
    ShortCodeLinkFix sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        const QString input = QStringLiteral("[LINKFIX url=\"https://a.com\" target=\"_blank\"]link[/LINKFIX]");
        sc.addCode(input, html, css, js, cssDoneIds, jsDoneIds);
    }));
}

// =============================================================================
// ShortCodeLinkTr — identity & argument contract
// =============================================================================

void Test_Website_ShortCodes::test_linktr_tag_name()
{
    ShortCodeLinkTr sc;
    QCOMPARE(sc.getTag(), QStringLiteral("LINKTR"));
}

void Test_Website_ShortCodes::test_linktr_available_arguments_count()
{
    ShortCodeLinkTr sc;
    QCOMPARE(sc.availableArguments().size(), 2);
}

void Test_Website_ShortCodes::test_linktr_url_argument_is_mandatory()
{
    ShortCodeLinkTr sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 1);
    QVERIFY(args.at(0).mandatory);
}

void Test_Website_ShortCodes::test_linktr_url_argument_translatable_optional()
{
    ShortCodeLinkTr sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 1);
    QVERIFY(args.at(0).translatable == AbstractShortCode::Translatable::Optional);
}

void Test_Website_ShortCodes::test_linktr_rel_argument_id()
{
    ShortCodeLinkTr sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 2);
    QCOMPARE(args.at(1).id, QStringLiteral("rel"));
}

void Test_Website_ShortCodes::test_linktr_rel_argument_not_mandatory()
{
    ShortCodeLinkTr sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 2);
    QVERIFY(!args.at(1).mandatory);
}

// =============================================================================
// ShortCodeLinkTr — registry & factory
// =============================================================================

void Test_Website_ShortCodes::test_linktr_registered_in_all_shortcodes()
{
    const auto &all = AbstractShortCode::ALL_SHORTCODES();
    QVERIFY(all.contains(QStringLiteral("LINKTR")));
    QVERIFY(all.value(QStringLiteral("LINKTR")) != nullptr);
}

void Test_Website_ShortCodes::test_linktr_for_tag_returns_instance()
{
    const AbstractShortCode *sc = AbstractShortCode::forTag(u"LINKTR");
    QVERIFY(sc != nullptr);
    QCOMPARE(sc->getTag(), QStringLiteral("LINKTR"));
}

// =============================================================================
// ShortCodeLinkTr — addCode output
// =============================================================================

void Test_Website_ShortCodes::test_linktr_add_code_url_in_html()
{
    ShortCodeLinkTr sc;
    const QString input = QStringLiteral("[LINKTR url=\"https://example.fr/produit\"]voir[/LINKTR]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("https://example.fr/produit")));
}

void Test_Website_ShortCodes::test_linktr_add_code_inner_content_in_html()
{
    ShortCodeLinkTr sc;
    const QString input = QStringLiteral("[LINKTR url=\"https://example.fr\"]middle text[/LINKTR]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("middle text")));
}

void Test_Website_ShortCodes::test_linktr_add_code_default_rel_dofollow()
{
    ShortCodeLinkTr sc;
    const QString input = QStringLiteral("[LINKTR url=\"https://example.fr\"]link[/LINKTR]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("rel=\"dofollow\"")));
}

void Test_Website_ShortCodes::test_linktr_add_code_custom_rel_sponsored()
{
    ShortCodeLinkTr sc;
    const QString input = QStringLiteral("[LINKTR url=\"https://example.fr\" rel=\"sponsored\"]link[/LINKTR]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("rel=\"sponsored\"")));
}

void Test_Website_ShortCodes::test_linktr_add_code_custom_rel_nofollow()
{
    ShortCodeLinkTr sc;
    const QString input = QStringLiteral("[LINKTR url=\"https://example.fr\" rel=\"nofollow\"]link[/LINKTR]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("rel=\"nofollow\"")));
}

void Test_Website_ShortCodes::test_linktr_add_code_does_not_touch_css()
{
    ShortCodeLinkTr sc;
    QString html, js;
    QString css = QStringLiteral("existing-css");
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[LINKTR url=\"https://example.fr\"]link[/LINKTR]"),
               html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(css, QStringLiteral("existing-css"));
    QVERIFY(cssDoneIds.isEmpty());
}

void Test_Website_ShortCodes::test_linktr_add_code_does_not_touch_js()
{
    ShortCodeLinkTr sc;
    QString html, css;
    QString js = QStringLiteral("existing-js");
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[LINKTR url=\"https://example.fr\"]link[/LINKTR]"),
               html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(js, QStringLiteral("existing-js"));
    QVERIFY(jsDoneIds.isEmpty());
}

void Test_Website_ShortCodes::test_linktr_add_code_via_for_tag()
{
    const AbstractShortCode *sc = AbstractShortCode::forTag(u"LINKTR");
    QVERIFY(sc != nullptr);
    const QString input = QStringLiteral("[LINKTR url=\"https://example.fr\"]via registry[/LINKTR]");
    const QString html = htmlFrom(*sc, input);
    QVERIFY(html.contains(QStringLiteral("https://example.fr")));
    QVERIFY(html.contains(QStringLiteral("via registry")));
}

// =============================================================================
// ShortCodeLinkTr — error cases
// =============================================================================

void Test_Website_ShortCodes::test_linktr_add_code_missing_url_throws()
{
    ShortCodeLinkTr sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[LINKTR]link[/LINKTR]"), html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_linktr_add_code_empty_url_throws()
{
    ShortCodeLinkTr sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[LINKTR url=\"\"]link[/LINKTR]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_linktr_add_code_empty_rel_throws()
{
    ShortCodeLinkTr sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        const QString input = QStringLiteral("[LINKTR url=\"https://example.fr\" rel=\"\"]link[/LINKTR]");
        sc.addCode(input, html, css, js, cssDoneIds, jsDoneIds);
    }));
}

// =============================================================================
// ShortCodeImageFix — identity & argument contract
// =============================================================================

void Test_Website_ShortCodes::test_imgfix_tag_name()
{
    ShortCodeImageFix sc;
    QCOMPARE(sc.getTag(), QStringLiteral("IMGFIX"));
}

void Test_Website_ShortCodes::test_imgfix_available_arguments_count()
{
    ShortCodeImageFix sc;
    QCOMPARE(sc.availableArguments().size(), 5);
}

void Test_Website_ShortCodes::test_imgfix_id_argument_id()
{
    ShortCodeImageFix sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 1);
    QCOMPARE(args.at(0).id, QStringLiteral("id"));
}

void Test_Website_ShortCodes::test_imgfix_id_argument_is_mandatory()
{
    ShortCodeImageFix sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 1);
    QVERIFY(args.at(0).mandatory);
}

void Test_Website_ShortCodes::test_imgfix_id_argument_not_translatable()
{
    ShortCodeImageFix sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 1);
    QVERIFY(args.at(0).translatable == AbstractShortCode::Translatable::No);
}

void Test_Website_ShortCodes::test_imgfix_filename_argument_id()
{
    ShortCodeImageFix sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 2);
    QCOMPARE(args.at(1).id, QStringLiteral("fileName"));
}

void Test_Website_ShortCodes::test_imgfix_filename_argument_is_mandatory()
{
    ShortCodeImageFix sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 2);
    QVERIFY(args.at(1).mandatory);
}

void Test_Website_ShortCodes::test_imgfix_filename_argument_translatable_optional()
{
    ShortCodeImageFix sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 2);
    QVERIFY(args.at(1).translatable == AbstractShortCode::Translatable::Optional);
}

void Test_Website_ShortCodes::test_imgfix_alt_argument_id()
{
    ShortCodeImageFix sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 3);
    QCOMPARE(args.at(2).id, QStringLiteral("alt"));
}

void Test_Website_ShortCodes::test_imgfix_alt_argument_is_mandatory()
{
    ShortCodeImageFix sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 3);
    QVERIFY(args.at(2).mandatory);
}

void Test_Website_ShortCodes::test_imgfix_alt_argument_translatable_yes()
{
    ShortCodeImageFix sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 3);
    QVERIFY(args.at(2).translatable == AbstractShortCode::Translatable::Yes);
}

void Test_Website_ShortCodes::test_imgfix_width_argument_not_mandatory()
{
    ShortCodeImageFix sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 4);
    QVERIFY(!args.at(3).mandatory);
}

void Test_Website_ShortCodes::test_imgfix_height_argument_not_mandatory()
{
    ShortCodeImageFix sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 5);
    QVERIFY(!args.at(4).mandatory);
}

// =============================================================================
// ShortCodeImageFix — registry & factory
// =============================================================================

void Test_Website_ShortCodes::test_imgfix_registered_in_all_shortcodes()
{
    const auto &all = AbstractShortCode::ALL_SHORTCODES();
    QVERIFY(all.contains(QStringLiteral("IMGFIX")));
    QVERIFY(all.value(QStringLiteral("IMGFIX")) != nullptr);
}

void Test_Website_ShortCodes::test_imgfix_for_tag_returns_instance()
{
    const AbstractShortCode *sc = AbstractShortCode::forTag(u"IMGFIX");
    QVERIFY(sc != nullptr);
    QCOMPARE(sc->getTag(), QStringLiteral("IMGFIX"));
}

// =============================================================================
// ShortCodeImageFix — addCode output
// =============================================================================

void Test_Website_ShortCodes::test_imgfix_add_code_alt_in_html()
{
    ShortCodeImageFix sc;
    const QString input = QStringLiteral(
        "[IMGFIX id=\"hero\" fileName=\"hero.jpg\" alt=\"Hero banner\"][/IMGFIX]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("Hero banner")));
}

void Test_Website_ShortCodes::test_imgfix_add_code_src_is_todo_webp()
{
    ShortCodeImageFix sc;
    const QString input = QStringLiteral(
        "[IMGFIX id=\"hero\" fileName=\"hero.jpg\" alt=\"Banner\"][/IMGFIX]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("src=\"TODO.webp\"")));
}

void Test_Website_ShortCodes::test_imgfix_add_code_width_in_html()
{
    ShortCodeImageFix sc;
    const QString input = QStringLiteral(
        "[IMGFIX id=\"i\" fileName=\"f.jpg\" alt=\"A\" width=\"800\"][/IMGFIX]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("width=\"800\"")));
}

void Test_Website_ShortCodes::test_imgfix_add_code_height_in_html()
{
    ShortCodeImageFix sc;
    const QString input = QStringLiteral(
        "[IMGFIX id=\"i\" fileName=\"f.jpg\" alt=\"A\" height=\"600\"][/IMGFIX]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("height=\"600\"")));
}

void Test_Website_ShortCodes::test_imgfix_add_code_width_absent_not_in_html()
{
    ShortCodeImageFix sc;
    const QString input = QStringLiteral(
        "[IMGFIX id=\"i\" fileName=\"f.jpg\" alt=\"A\"][/IMGFIX]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(!html.contains(QStringLiteral("width=")));
}

void Test_Website_ShortCodes::test_imgfix_add_code_height_absent_not_in_html()
{
    ShortCodeImageFix sc;
    const QString input = QStringLiteral(
        "[IMGFIX id=\"i\" fileName=\"f.jpg\" alt=\"A\"][/IMGFIX]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(!html.contains(QStringLiteral("height=")));
}

void Test_Website_ShortCodes::test_imgfix_add_code_appends_to_existing_html()
{
    ShortCodeImageFix sc;
    const QString prefix = QStringLiteral("<p>before</p>");
    QString html = prefix;
    QString css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[IMGFIX id=\"i\" fileName=\"f.jpg\" alt=\"A\"][/IMGFIX]"),
               html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.startsWith(prefix));
    QVERIFY(html.size() > prefix.size());
}

void Test_Website_ShortCodes::test_imgfix_add_code_does_not_touch_css()
{
    ShortCodeImageFix sc;
    QString html, js;
    QString css = QStringLiteral("existing-css");
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[IMGFIX id=\"i\" fileName=\"f.jpg\" alt=\"A\"][/IMGFIX]"),
               html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(css, QStringLiteral("existing-css"));
    QVERIFY(cssDoneIds.isEmpty());
}

void Test_Website_ShortCodes::test_imgfix_add_code_does_not_touch_js()
{
    ShortCodeImageFix sc;
    QString html, css;
    QString js = QStringLiteral("existing-js");
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[IMGFIX id=\"i\" fileName=\"f.jpg\" alt=\"A\"][/IMGFIX]"),
               html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(js, QStringLiteral("existing-js"));
    QVERIFY(jsDoneIds.isEmpty());
}

void Test_Website_ShortCodes::test_imgfix_add_code_via_for_tag()
{
    const AbstractShortCode *sc = AbstractShortCode::forTag(u"IMGFIX");
    QVERIFY(sc != nullptr);
    const QString input = QStringLiteral(
        "[IMGFIX id=\"hero\" fileName=\"hero.jpg\" alt=\"Via registry\"][/IMGFIX]");
    const QString html = htmlFrom(*sc, input);
    QVERIFY(html.contains(QStringLiteral("Via registry")));
    QVERIFY(html.contains(QStringLiteral("src=\"TODO.webp\"")));
}

// =============================================================================
// ShortCodeImageFix — error cases
// =============================================================================

void Test_Website_ShortCodes::test_imgfix_add_code_missing_id_throws()
{
    ShortCodeImageFix sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[IMGFIX fileName=\"f.jpg\" alt=\"A\"][/IMGFIX]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_imgfix_add_code_missing_filename_throws()
{
    ShortCodeImageFix sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[IMGFIX id=\"i\" alt=\"A\"][/IMGFIX]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_imgfix_add_code_missing_alt_throws()
{
    ShortCodeImageFix sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[IMGFIX id=\"i\" fileName=\"f.jpg\"][/IMGFIX]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_imgfix_add_code_empty_alt_throws()
{
    ShortCodeImageFix sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[IMGFIX id=\"i\" fileName=\"f.jpg\" alt=\"\"][/IMGFIX]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_imgfix_add_code_invalid_width_throws()
{
    ShortCodeImageFix sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[IMGFIX id=\"i\" fileName=\"f.jpg\" alt=\"A\" width=\"abc\"][/IMGFIX]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_imgfix_add_code_invalid_height_throws()
{
    ShortCodeImageFix sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[IMGFIX id=\"i\" fileName=\"f.jpg\" alt=\"A\" height=\"6.5\"][/IMGFIX]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_imgfix_add_code_unknown_argument_throws()
{
    ShortCodeImageFix sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[IMGFIX id=\"i\" fileName=\"f.jpg\" alt=\"A\" class=\"hero\"][/IMGFIX]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_imgfix_add_code_mismatched_tags_throws()
{
    ShortCodeImageFix sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[IMGFIX id=\"i\" fileName=\"f.jpg\" alt=\"A\"][/IMGTR]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

// =============================================================================
// ShortCodeImageTr — identity & argument contract
// =============================================================================

void Test_Website_ShortCodes::test_imgtr_tag_name()
{
    ShortCodeImageTr sc;
    QCOMPARE(sc.getTag(), QStringLiteral("IMGTR"));
}

void Test_Website_ShortCodes::test_imgtr_available_arguments_count()
{
    ShortCodeImageTr sc;
    QCOMPARE(sc.availableArguments().size(), 5);
}

void Test_Website_ShortCodes::test_imgtr_id_argument_translatable_yes()
{
    // Key difference from IMGFIX: id is Translatable::Yes in IMGTR.
    ShortCodeImageTr sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 1);
    QVERIFY(args.at(0).translatable == AbstractShortCode::Translatable::Yes);
}

void Test_Website_ShortCodes::test_imgtr_filename_argument_translatable_yes()
{
    // Key difference from IMGFIX: fileName is Translatable::Yes in IMGTR.
    ShortCodeImageTr sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 2);
    QVERIFY(args.at(1).translatable == AbstractShortCode::Translatable::Yes);
}

void Test_Website_ShortCodes::test_imgtr_alt_argument_translatable_yes()
{
    ShortCodeImageTr sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 3);
    QVERIFY(args.at(2).translatable == AbstractShortCode::Translatable::Yes);
}

void Test_Website_ShortCodes::test_imgtr_width_argument_not_mandatory()
{
    ShortCodeImageTr sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 4);
    QVERIFY(!args.at(3).mandatory);
}

void Test_Website_ShortCodes::test_imgtr_height_argument_not_mandatory()
{
    ShortCodeImageTr sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 5);
    QVERIFY(!args.at(4).mandatory);
}

// =============================================================================
// ShortCodeImageTr — registry & factory
// =============================================================================

void Test_Website_ShortCodes::test_imgtr_registered_in_all_shortcodes()
{
    const auto &all = AbstractShortCode::ALL_SHORTCODES();
    QVERIFY(all.contains(QStringLiteral("IMGTR")));
    QVERIFY(all.value(QStringLiteral("IMGTR")) != nullptr);
}

void Test_Website_ShortCodes::test_imgtr_for_tag_returns_instance()
{
    const AbstractShortCode *sc = AbstractShortCode::forTag(u"IMGTR");
    QVERIFY(sc != nullptr);
    QCOMPARE(sc->getTag(), QStringLiteral("IMGTR"));
}

// =============================================================================
// ShortCodeImageTr — addCode output
// =============================================================================

void Test_Website_ShortCodes::test_imgtr_add_code_alt_in_html()
{
    ShortCodeImageTr sc;
    const QString input = QStringLiteral(
        "[IMGTR id=\"hero-fr\" fileName=\"hero_fr.jpg\" alt=\"Banniere principale\"][/IMGTR]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("Banniere principale")));
}

void Test_Website_ShortCodes::test_imgtr_add_code_src_is_todo_webp()
{
    ShortCodeImageTr sc;
    const QString input = QStringLiteral(
        "[IMGTR id=\"hero-fr\" fileName=\"hero_fr.jpg\" alt=\"Alt\"][/IMGTR]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("src=\"TODO.webp\"")));
}

void Test_Website_ShortCodes::test_imgtr_add_code_with_width_and_height()
{
    ShortCodeImageTr sc;
    const QString input = QStringLiteral(
        "[IMGTR id=\"i\" fileName=\"f.jpg\" alt=\"A\" width=\"1200\" height=\"400\"][/IMGTR]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("width=\"1200\"")));
    QVERIFY(html.contains(QStringLiteral("height=\"400\"")));
}

void Test_Website_ShortCodes::test_imgtr_add_code_width_absent_not_in_html()
{
    ShortCodeImageTr sc;
    const QString input = QStringLiteral(
        "[IMGTR id=\"i\" fileName=\"f.jpg\" alt=\"A\"][/IMGTR]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(!html.contains(QStringLiteral("width=")));
}

void Test_Website_ShortCodes::test_imgtr_add_code_appends_to_existing_html()
{
    ShortCodeImageTr sc;
    const QString prefix = QStringLiteral("<section>");
    QString html = prefix;
    QString css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[IMGTR id=\"i\" fileName=\"f.jpg\" alt=\"A\"][/IMGTR]"),
               html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.startsWith(prefix));
    QVERIFY(html.size() > prefix.size());
}

void Test_Website_ShortCodes::test_imgtr_add_code_does_not_touch_css()
{
    ShortCodeImageTr sc;
    QString html, js;
    QString css = QStringLiteral("existing-css");
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[IMGTR id=\"i\" fileName=\"f.jpg\" alt=\"A\"][/IMGTR]"),
               html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(css, QStringLiteral("existing-css"));
    QVERIFY(cssDoneIds.isEmpty());
}

void Test_Website_ShortCodes::test_imgtr_add_code_does_not_touch_js()
{
    ShortCodeImageTr sc;
    QString html, css;
    QString js = QStringLiteral("existing-js");
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[IMGTR id=\"i\" fileName=\"f.jpg\" alt=\"A\"][/IMGTR]"),
               html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(js, QStringLiteral("existing-js"));
    QVERIFY(jsDoneIds.isEmpty());
}

void Test_Website_ShortCodes::test_imgtr_add_code_via_for_tag()
{
    const AbstractShortCode *sc = AbstractShortCode::forTag(u"IMGTR");
    QVERIFY(sc != nullptr);
    const QString input = QStringLiteral(
        "[IMGTR id=\"hero-fr\" fileName=\"hero_fr.jpg\" alt=\"Via registry\"][/IMGTR]");
    const QString html = htmlFrom(*sc, input);
    QVERIFY(html.contains(QStringLiteral("Via registry")));
    QVERIFY(html.contains(QStringLiteral("src=\"TODO.webp\"")));
}

// =============================================================================
// ShortCodeImageTr — error cases
// =============================================================================

void Test_Website_ShortCodes::test_imgtr_add_code_missing_id_throws()
{
    ShortCodeImageTr sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[IMGTR fileName=\"f.jpg\" alt=\"A\"][/IMGTR]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_imgtr_add_code_missing_filename_throws()
{
    ShortCodeImageTr sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[IMGTR id=\"i\" alt=\"A\"][/IMGTR]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_imgtr_add_code_missing_alt_throws()
{
    ShortCodeImageTr sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[IMGTR id=\"i\" fileName=\"f.jpg\"][/IMGTR]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_imgtr_add_code_empty_alt_throws()
{
    ShortCodeImageTr sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[IMGTR id=\"i\" fileName=\"f.jpg\" alt=\"\"][/IMGTR]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_imgtr_add_code_invalid_width_throws()
{
    ShortCodeImageTr sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[IMGTR id=\"i\" fileName=\"f.jpg\" alt=\"A\" width=\"50%\"][/IMGTR]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_imgtr_add_code_invalid_height_throws()
{
    ShortCodeImageTr sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[IMGTR id=\"i\" fileName=\"f.jpg\" alt=\"A\" height=\"auto\"][/IMGTR]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_imgtr_add_code_unknown_argument_throws()
{
    ShortCodeImageTr sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[IMGTR id=\"i\" fileName=\"f.jpg\" alt=\"A\" loading=\"lazy\"][/IMGTR]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_imgtr_add_code_mismatched_tags_throws()
{
    ShortCodeImageTr sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[IMGTR id=\"i\" fileName=\"f.jpg\" alt=\"A\"][/IMGFIX]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

// =============================================================================
// ShortCodeSpinnable — identity & argument contract
// =============================================================================

void Test_Website_ShortCodes::test_spinnable_tag_name()
{
    ShortCodeSpinnable sc;
    QCOMPARE(sc.getTag(), QStringLiteral("SPINNABLE"));
}

void Test_Website_ShortCodes::test_spinnable_available_arguments_count()
{
    ShortCodeSpinnable sc;
    QCOMPARE(sc.availableArguments().size(), 2);
}

void Test_Website_ShortCodes::test_spinnable_id_argument_id()
{
    ShortCodeSpinnable sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 1);
    QCOMPARE(args.at(0).id, QStringLiteral("id"));
}

void Test_Website_ShortCodes::test_spinnable_id_argument_is_mandatory()
{
    ShortCodeSpinnable sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 1);
    QVERIFY(args.at(0).mandatory);
}

void Test_Website_ShortCodes::test_spinnable_id_argument_not_translatable()
{
    ShortCodeSpinnable sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 1);
    QVERIFY(args.at(0).translatable == AbstractShortCode::Translatable::No);
}

void Test_Website_ShortCodes::test_spinnable_random_argument_id()
{
    ShortCodeSpinnable sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 2);
    QCOMPARE(args.at(1).id, QStringLiteral("random"));
}

void Test_Website_ShortCodes::test_spinnable_random_argument_not_mandatory()
{
    ShortCodeSpinnable sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 2);
    QVERIFY(!args.at(1).mandatory);
}

void Test_Website_ShortCodes::test_spinnable_random_argument_allowed_values()
{
    ShortCodeSpinnable sc;
    const auto &args = sc.availableArguments();
    QVERIFY(args.size() >= 2);
    const QStringList &allowed = args.at(1).allowedValues;
    QVERIFY(allowed.contains(QStringLiteral("true")));
    QVERIFY(allowed.contains(QStringLiteral("false")));
}

// =============================================================================
// ShortCodeSpinnable — registry & factory
// =============================================================================

void Test_Website_ShortCodes::test_spinnable_registered_in_all_shortcodes()
{
    const auto &all = AbstractShortCode::ALL_SHORTCODES();
    QVERIFY(all.contains(QStringLiteral("SPINNABLE")));
    QVERIFY(all.value(QStringLiteral("SPINNABLE")) != nullptr);
}

void Test_Website_ShortCodes::test_spinnable_for_tag_returns_instance()
{
    const AbstractShortCode *sc = AbstractShortCode::forTag(u"SPINNABLE");
    QVERIFY(sc != nullptr);
    QCOMPARE(sc->getTag(), QStringLiteral("SPINNABLE"));
}

// =============================================================================
// ShortCodeSpinnable — addCode output
// =============================================================================

void Test_Website_ShortCodes::test_spinnable_add_code_result_is_one_of_options()
{
    ShortCodeSpinnable sc;
    const QString input = QStringLiteral("[SPINNABLE id=\"1\"]{alpha|beta|gamma}[/SPINNABLE]");
    const QString html = htmlFrom(sc, input);
    const bool valid = html == QStringLiteral("alpha")
                    || html == QStringLiteral("beta")
                    || html == QStringLiteral("gamma");
    QVERIFY(valid);
    QVERIFY(!html.isEmpty());
    QVERIFY(!html.contains(QStringLiteral("{")));
}

void Test_Website_ShortCodes::test_spinnable_add_code_fixed_seed_deterministic()
{
    // Same id (same seed) must produce the same result across independent calls.
    ShortCodeSpinnable sc;
    const QString input = QStringLiteral("[SPINNABLE id=\"42\"]{hello|world|foo|bar}[/SPINNABLE]");
    const QString first  = htmlFrom(sc, input);
    const QString second = htmlFrom(sc, input);
    QCOMPARE(first, second);
}

void Test_Website_ShortCodes::test_spinnable_add_code_result_has_no_braces()
{
    ShortCodeSpinnable sc;
    const QString input = QStringLiteral("[SPINNABLE id=\"3\"]{a|b} and {c|d}[/SPINNABLE]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(!html.contains(QStringLiteral("{")));
    QVERIFY(!html.contains(QStringLiteral("}")));
}

void Test_Website_ShortCodes::test_spinnable_add_code_plain_text_preserved()
{
    // Text outside spin groups must be copied verbatim.
    ShortCodeSpinnable sc;
    const QString input = QStringLiteral("[SPINNABLE id=\"2\"]prefix {x|y} suffix[/SPINNABLE]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.startsWith(QStringLiteral("prefix ")));
    QVERIFY(html.endsWith(QStringLiteral(" suffix")));
}

void Test_Website_ShortCodes::test_spinnable_add_code_nested_group()
{
    // {a|{b|c}} — outer picks between "a" and one of "b"/"c".
    ShortCodeSpinnable sc;
    const QString input = QStringLiteral("[SPINNABLE id=\"5\"]{a|{b|c}}[/SPINNABLE]");
    const QString html = htmlFrom(sc, input);
    const bool valid = html == QStringLiteral("a")
                    || html == QStringLiteral("b")
                    || html == QStringLiteral("c");
    QVERIFY(valid);
    QVERIFY(!html.contains(QStringLiteral("{")));
    QVERIFY(!html.contains(QStringLiteral("}")));
}

void Test_Website_ShortCodes::test_spinnable_add_code_multiple_groups()
{
    // Two independent groups separated by "-"; result is one of four combinations.
    ShortCodeSpinnable sc;
    const QString input = QStringLiteral("[SPINNABLE id=\"10\"]{foo|bar}-{baz|qux}[/SPINNABLE]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("-")));
    const bool valid = html == QStringLiteral("foo-baz")
                    || html == QStringLiteral("foo-qux")
                    || html == QStringLiteral("bar-baz")
                    || html == QStringLiteral("bar-qux");
    QVERIFY(valid);
}

void Test_Website_ShortCodes::test_spinnable_add_code_appends_to_existing_html()
{
    ShortCodeSpinnable sc;
    const QString prefix = QStringLiteral("<p>before</p>");
    QString html = prefix;
    QString css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[SPINNABLE id=\"1\"]{hello|world}[/SPINNABLE]"),
               html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.startsWith(prefix));
    QVERIFY(html.size() > prefix.size());
}

void Test_Website_ShortCodes::test_spinnable_add_code_does_not_touch_css()
{
    ShortCodeSpinnable sc;
    QString html, js;
    QString css = QStringLiteral("existing-css");
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[SPINNABLE id=\"1\"]{a|b}[/SPINNABLE]"),
               html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(css, QStringLiteral("existing-css"));
    QVERIFY(cssDoneIds.isEmpty());
}

void Test_Website_ShortCodes::test_spinnable_add_code_does_not_touch_js()
{
    ShortCodeSpinnable sc;
    QString html, css;
    QString js = QStringLiteral("existing-js");
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[SPINNABLE id=\"1\"]{a|b}[/SPINNABLE]"),
               html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(js, QStringLiteral("existing-js"));
    QVERIFY(jsDoneIds.isEmpty());
}

void Test_Website_ShortCodes::test_spinnable_add_code_shortcode_in_content_preserved()
{
    // Spinning must not corrupt embedded shortcode tags; [ ] are passed through verbatim.
    ShortCodeSpinnable sc;
    const QString input = QStringLiteral(
        "[SPINNABLE id=\"5\"]{See|Watch} [VIDEO url=\"https://example.com/vid\"][/VIDEO][/SPINNABLE]");
    const QString html = htmlFrom(sc, input);
    QVERIFY(html.contains(QStringLiteral("[VIDEO url=\"https://example.com/vid\"][/VIDEO]")));
    QVERIFY(html.contains(QStringLiteral("https://example.com/vid")));
}

void Test_Website_ShortCodes::test_spinnable_add_code_random_flag_output_valid()
{
    // random=true means the RNG is not seeded with id — output still valid.
    ShortCodeSpinnable sc;
    const QString input = QStringLiteral("[SPINNABLE id=\"1\" random=\"true\"]{yes|no}[/SPINNABLE]");
    const QString html = htmlFrom(sc, input);
    const bool valid = html == QStringLiteral("yes") || html == QStringLiteral("no");
    QVERIFY(valid);
    QVERIFY(!html.contains(QStringLiteral("{")));
}

void Test_Website_ShortCodes::test_spinnable_add_code_via_for_tag()
{
    const AbstractShortCode *sc = AbstractShortCode::forTag(u"SPINNABLE");
    QVERIFY(sc != nullptr);
    const QString input = QStringLiteral("[SPINNABLE id=\"99\"]{ping|pong}[/SPINNABLE]");
    const QString html = htmlFrom(*sc, input);
    const bool valid = html == QStringLiteral("ping") || html == QStringLiteral("pong");
    QVERIFY(valid);
}

// =============================================================================
// ShortCodeSpinnable — argument validation errors
// =============================================================================

void Test_Website_ShortCodes::test_spinnable_add_code_missing_id_throws()
{
    ShortCodeSpinnable sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[SPINNABLE]{a|b}[/SPINNABLE]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_spinnable_add_code_id_not_digits_throws()
{
    ShortCodeSpinnable sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[SPINNABLE id=\"abc\"]{a|b}[/SPINNABLE]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_spinnable_add_code_id_negative_throws()
{
    ShortCodeSpinnable sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[SPINNABLE id=\"-5\"]{a|b}[/SPINNABLE]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_spinnable_add_code_id_float_throws()
{
    ShortCodeSpinnable sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[SPINNABLE id=\"1.5\"]{a|b}[/SPINNABLE]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_spinnable_add_code_invalid_random_value_throws()
{
    ShortCodeSpinnable sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[SPINNABLE id=\"1\" random=\"yes\"]{a|b}[/SPINNABLE]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_spinnable_add_code_unknown_argument_throws()
{
    ShortCodeSpinnable sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[SPINNABLE id=\"1\" seed=\"42\"]{a|b}[/SPINNABLE]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

// =============================================================================
// ShortCodeSpinnable — spinning syntax errors
// =============================================================================

void Test_Website_ShortCodes::test_spinnable_add_code_no_spin_group_throws()
{
    // Plain text with no { } groups must be rejected.
    ShortCodeSpinnable sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[SPINNABLE id=\"1\"]just plain text[/SPINNABLE]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_spinnable_add_code_unmatched_open_brace_throws()
{
    ShortCodeSpinnable sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        // {a|b is missing the closing }
        sc.addCode(QStringLiteral("[SPINNABLE id=\"1\"]{a|b unclosed[/SPINNABLE]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_spinnable_add_code_unmatched_close_brace_throws()
{
    ShortCodeSpinnable sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        // stray } with no matching {
        sc.addCode(QStringLiteral("[SPINNABLE id=\"1\"]{a|b} extra}[/SPINNABLE]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_spinnable_add_code_group_no_pipe_throws()
{
    // A group with no | is not a spin group.
    ShortCodeSpinnable sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[SPINNABLE id=\"1\"]{nopipe}[/SPINNABLE]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_spinnable_add_code_nested_unmatched_brace_throws()
{
    // The outer group is closed but the inner { is never closed.
    ShortCodeSpinnable sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[SPINNABLE id=\"1\"]{a|{b|c}[/SPINNABLE]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_spinnable_add_code_malformed_shortcode_throws()
{
    ShortCodeSpinnable sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("this is not a shortcode at all"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_spinnable_add_code_mismatched_tags_throws()
{
    ShortCodeSpinnable sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[SPINNABLE id=\"1\"]{a|b}[/VIDEO]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

// =============================================================================
// ShortCodeTitle — identity & argument contract
// =============================================================================

void Test_Website_ShortCodes::test_title_tag_name()
{
    ShortCodeTitle sc;
    QCOMPARE(sc.getTag(), QStringLiteral("TITLE"));
}

void Test_Website_ShortCodes::test_title_available_arguments_count()
{
    ShortCodeTitle sc;
    QCOMPARE(sc.availableArguments().size(), 1);
}

void Test_Website_ShortCodes::test_title_level_argument_id()
{
    ShortCodeTitle sc;
    const auto &args = sc.availableArguments();
    QVERIFY(!args.isEmpty());
    QCOMPARE(args.first().id, QStringLiteral("level"));
}

void Test_Website_ShortCodes::test_title_level_argument_is_mandatory()
{
    ShortCodeTitle sc;
    const auto &args = sc.availableArguments();
    QVERIFY(!args.isEmpty());
    QVERIFY(args.first().mandatory);
}

void Test_Website_ShortCodes::test_title_level_argument_has_six_allowed_values()
{
    ShortCodeTitle sc;
    const auto &args = sc.availableArguments();
    QVERIFY(!args.isEmpty());
    const QStringList &allowed = args.first().allowedValues;
    QCOMPARE(allowed.size(), 6);
    QVERIFY(allowed.contains(QStringLiteral("1")));
    QVERIFY(allowed.contains(QStringLiteral("6")));
}

void Test_Website_ShortCodes::test_title_level_argument_not_translatable()
{
    ShortCodeTitle sc;
    const auto &args = sc.availableArguments();
    QVERIFY(!args.isEmpty());
    QVERIFY(args.first().translatable == AbstractShortCode::Translatable::No);
}

// =============================================================================
// ShortCodeTitle — registry & factory
// =============================================================================

void Test_Website_ShortCodes::test_title_registered_in_all_shortcodes()
{
    const auto &all = AbstractShortCode::ALL_SHORTCODES();
    QVERIFY(all.contains(QStringLiteral("TITLE")));
    QVERIFY(all.value(QStringLiteral("TITLE")) != nullptr);
}

void Test_Website_ShortCodes::test_title_for_tag_returns_instance()
{
    const AbstractShortCode *sc = AbstractShortCode::forTag(u"TITLE");
    QVERIFY(sc != nullptr);
    QCOMPARE(sc->getTag(), QStringLiteral("TITLE"));
}

// =============================================================================
// ShortCodeTitle — addCode output
// =============================================================================

void Test_Website_ShortCodes::test_title_add_code_h1_tag_in_html()
{
    ShortCodeTitle sc;
    const QString html = htmlFrom(sc, QStringLiteral("[TITLE level=\"1\"]Hello[/TITLE]"));
    QCOMPARE(html, QStringLiteral("<h1>Hello</h1>"));
}

void Test_Website_ShortCodes::test_title_add_code_h3_tag_in_html()
{
    ShortCodeTitle sc;
    const QString html = htmlFrom(sc, QStringLiteral("[TITLE level=\"3\"]Section[/TITLE]"));
    QCOMPARE(html, QStringLiteral("<h3>Section</h3>"));
}

void Test_Website_ShortCodes::test_title_add_code_h6_tag_in_html()
{
    ShortCodeTitle sc;
    const QString html = htmlFrom(sc, QStringLiteral("[TITLE level=\"6\"]Fine print[/TITLE]"));
    QCOMPARE(html, QStringLiteral("<h6>Fine print</h6>"));
}

void Test_Website_ShortCodes::test_title_add_code_inner_content_in_html()
{
    ShortCodeTitle sc;
    const QString html = htmlFrom(sc, QStringLiteral("[TITLE level=\"2\"]My heading[/TITLE]"));
    QVERIFY(html.contains(QStringLiteral("My heading")));
}

void Test_Website_ShortCodes::test_title_add_code_appends_to_existing_html()
{
    ShortCodeTitle sc;
    const QString prefix = QStringLiteral("<p>before</p>");
    QString html = prefix;
    QString css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[TITLE level=\"1\"]Hi[/TITLE]"), html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.startsWith(prefix));
    QVERIFY(html.size() > prefix.size());
}

void Test_Website_ShortCodes::test_title_add_code_does_not_touch_css()
{
    ShortCodeTitle sc;
    QString html, js;
    QString css = QStringLiteral("existing-css");
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[TITLE level=\"1\"]Hi[/TITLE]"), html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(css, QStringLiteral("existing-css"));
    QVERIFY(cssDoneIds.isEmpty());
}

void Test_Website_ShortCodes::test_title_add_code_does_not_touch_js()
{
    ShortCodeTitle sc;
    QString html, css;
    QString js = QStringLiteral("existing-js");
    QSet<QString> cssDoneIds, jsDoneIds;
    sc.addCode(QStringLiteral("[TITLE level=\"1\"]Hi[/TITLE]"), html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(js, QStringLiteral("existing-js"));
    QVERIFY(jsDoneIds.isEmpty());
}

void Test_Website_ShortCodes::test_title_add_code_via_for_tag()
{
    const AbstractShortCode *sc = AbstractShortCode::forTag(u"TITLE");
    QVERIFY(sc != nullptr);
    const QString html = htmlFrom(*sc, QStringLiteral("[TITLE level=\"4\"]Via factory[/TITLE]"));
    QCOMPARE(html, QStringLiteral("<h4>Via factory</h4>"));
}

// =============================================================================
// ShortCodeTitle — validation error cases
// =============================================================================

void Test_Website_ShortCodes::test_title_add_code_missing_level_throws()
{
    ShortCodeTitle sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[TITLE]Hello[/TITLE]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_title_add_code_invalid_level_zero_throws()
{
    ShortCodeTitle sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[TITLE level=\"0\"]Hello[/TITLE]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_title_add_code_invalid_level_seven_throws()
{
    ShortCodeTitle sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[TITLE level=\"7\"]Hello[/TITLE]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_title_add_code_invalid_level_alpha_throws()
{
    ShortCodeTitle sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[TITLE level=\"h2\"]Hello[/TITLE]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_title_add_code_unknown_argument_throws()
{
    ShortCodeTitle sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[TITLE level=\"1\" class=\"big\"]Hello[/TITLE]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_title_add_code_mismatched_tags_throws()
{
    ShortCodeTitle sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[TITLE level=\"1\"]Hello[/VIDEO]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

void Test_Website_ShortCodes::test_title_add_code_duplicate_argument_throws()
{
    ShortCodeTitle sc;
    QVERIFY(throwsShortCodeException([&sc] {
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        sc.addCode(QStringLiteral("[TITLE level=\"1\" level=\"2\"]Hello[/TITLE]"),
                   html, css, js, cssDoneIds, jsDoneIds);
    }));
}

// =============================================================================

QTEST_MAIN(Test_Website_ShortCodes)
#include "test_website_shortcodes.moc"
