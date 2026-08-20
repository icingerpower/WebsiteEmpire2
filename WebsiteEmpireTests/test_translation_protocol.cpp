#include <QtTest>

#include "website/translation/TranslationProtocol.h"

class Test_TranslationProtocol : public QObject
{
    Q_OBJECT

private slots:
    // normalizeSlug -------------------------------------------------------

    void test_protocol_slug_lowercases_and_hyphenates();
    void test_protocol_slug_folds_accents_to_ascii();
    void test_protocol_slug_strips_invalid_characters();
    void test_protocol_slug_collapses_and_trims_hyphens();
    void test_protocol_slug_non_latin_script_returns_empty();
    void test_protocol_slug_preserves_html_extension_from_source();
    void test_protocol_slug_restores_extension_when_ai_omits_it();
    void test_protocol_slug_no_extension_when_source_has_none();
    void test_protocol_slug_dotted_source_does_not_fuse_extension();

    // parseResponse -------------------------------------------------------

    void test_protocol_parse_single_field();
    void test_protocol_parse_multiple_fields();
    void test_protocol_parse_multiline_text();
    void test_protocol_parse_text_with_embedded_quotes();
    void test_protocol_parse_text_with_shortcode_attributes();
    void test_protocol_parse_empty_response_returns_empty();
    void test_protocol_parse_ignores_preamble_text();
    void test_protocol_parse_id_trimmed();
    void test_protocol_parse_missing_end_marker_still_extracts_content();
    void test_protocol_parse_end_without_preceding_newline();
    void test_protocol_parse_duplicate_field_id_appends_content();
    void test_protocol_parse_continuation_suffix_stripped_and_appended();

    // buildPrompt ---------------------------------------------------------

    void test_protocol_build_contains_source_and_target_lang();
    void test_protocol_build_contains_field_id();
    void test_protocol_build_contains_source_text();
    void test_protocol_build_multiple_fields();
    void test_protocol_build_contains_begin_end_format_instructions();

    // round-trip ----------------------------------------------------------

    // The old JSON format failed when source text contained embedded quotes
    // (e.g. shortcode attributes like [TITLE level="1"]).  The delimiter format
    // must survive the round-trip even when translated text contains such quotes.
    void test_protocol_roundtrip_text_with_embedded_quotes_survives();

    // sanitizeShortName -----------------------------------------------------

    void test_protocol_sanitize_clean_value_returned_unchanged();
    void test_protocol_sanitize_healybio_ko_promptleak_rejected();
    void test_protocol_sanitize_healybio_it_duplicate_line_keeps_first();
    void test_protocol_sanitize_multiline_takes_first_nonempty_line();
    void test_protocol_sanitize_leading_blank_lines_skipped();
    void test_protocol_sanitize_all_blank_lines_rejected();
    void test_protocol_sanitize_marker_case_insensitive();
    void test_protocol_sanitize_implausibly_long_relative_to_source_rejected();
    void test_protocol_sanitize_verbose_translation_within_ratio_accepted();
};

// =============================================================================
// parseResponse
// =============================================================================

void Test_TranslationProtocol::test_protocol_parse_single_field()
{
    const QString response =
        QStringLiteral("===BEGIN 1_text===\nHello world\n===END===");
    const auto result = TranslationProtocol::parseResponse(response);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.value(QStringLiteral("1_text")), QStringLiteral("Hello world"));
}

void Test_TranslationProtocol::test_protocol_parse_multiple_fields()
{
    const QString response =
        QStringLiteral("===BEGIN 0_title===\nMon Titre\n===END===\n"
                       "===BEGIN 1_text===\nMon texte\n===END===");
    const auto result = TranslationProtocol::parseResponse(response);
    QCOMPARE(result.size(), 2);
    QCOMPARE(result.value(QStringLiteral("0_title")), QStringLiteral("Mon Titre"));
    QCOMPARE(result.value(QStringLiteral("1_text")), QStringLiteral("Mon texte"));
}

void Test_TranslationProtocol::test_protocol_parse_multiline_text()
{
    const QString response =
        QStringLiteral("===BEGIN 1_text===\nLine one.\nLine two.\nLine three.\n===END===");
    const auto result = TranslationProtocol::parseResponse(response);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.value(QStringLiteral("1_text")),
             QStringLiteral("Line one.\nLine two.\nLine three."));
}

void Test_TranslationProtocol::test_protocol_parse_text_with_embedded_quotes()
{
    // Translated text contains double-quote characters — no escaping required.
    const QString response =
        QStringLiteral("===BEGIN 1_text===\nIl dit \"bonjour\".\n===END===");
    const auto result = TranslationProtocol::parseResponse(response);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.value(QStringLiteral("1_text")), QStringLiteral("Il dit \"bonjour\"."));
}

void Test_TranslationProtocol::test_protocol_parse_text_with_shortcode_attributes()
{
    // This is the exact bug scenario: shortcode attrs contain level="1" which
    // would break JSON parsing.  The delimiter format handles it transparently.
    const QString response =
        QStringLiteral("===BEGIN 1_text===\n"
                       "[TITRE niveau=\"1\"]Mon article[/TITRE]\n"
                       "Texte du paragraphe.\n"
                       "===END===");
    const auto result = TranslationProtocol::parseResponse(response);
    QCOMPARE(result.size(), 1);
    const QString expected =
        QStringLiteral("[TITRE niveau=\"1\"]Mon article[/TITRE]\nTexte du paragraphe.");
    QCOMPARE(result.value(QStringLiteral("1_text")), expected);
}

void Test_TranslationProtocol::test_protocol_parse_empty_response_returns_empty()
{
    QVERIFY(TranslationProtocol::parseResponse(QString()).isEmpty());
    QVERIFY(TranslationProtocol::parseResponse(QStringLiteral("No blocks here.")).isEmpty());
}

void Test_TranslationProtocol::test_protocol_parse_ignores_preamble_text()
{
    // Claude sometimes writes an explanatory sentence before the blocks.
    const QString response =
        QStringLiteral("Here is the translation:\n\n"
                       "===BEGIN 1_text===\nBonjour\n===END===\n\n"
                       "Hope that helps!");
    const auto result = TranslationProtocol::parseResponse(response);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.value(QStringLiteral("1_text")), QStringLiteral("Bonjour"));
}

void Test_TranslationProtocol::test_protocol_parse_id_trimmed()
{
    // Spaces around the id must be stripped.
    const QString response =
        QStringLiteral("===BEGIN  1_text  ===\nBonjour\n===END===");
    const auto result = TranslationProtocol::parseResponse(response);
    QCOMPARE(result.size(), 1);
    QVERIFY(result.contains(QStringLiteral("1_text")));
}

void Test_TranslationProtocol::test_protocol_parse_missing_end_marker_still_extracts_content()
{
    // Simulate truncated output: ===END=== for the last block is missing.
    // The parser must still return whatever content it found.
    const QString response =
        QStringLiteral("===BEGIN 0_title===\nMon Titre\n===END===\n"
                       "===BEGIN 1_text===\nTexte tronqué sans marqueur de fin");
    const auto result = TranslationProtocol::parseResponse(response);
    QCOMPARE(result.size(), 2);
    QCOMPARE(result.value(QStringLiteral("0_title")), QStringLiteral("Mon Titre"));
    QCOMPARE(result.value(QStringLiteral("1_text")), QStringLiteral("Texte tronqué sans marqueur de fin"));
}

void Test_TranslationProtocol::test_protocol_parse_end_without_preceding_newline()
{
    // Claude may write the last word immediately followed by ===END=== with no newline.
    const QString response =
        QStringLiteral("===BEGIN 1_text===\nBonjour monde.===END===");
    const auto result = TranslationProtocol::parseResponse(response);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.value(QStringLiteral("1_text")), QStringLiteral("Bonjour monde."));
}

void Test_TranslationProtocol::test_protocol_parse_duplicate_field_id_appends_content()
{
    // Simulate a CLI continuation: the model hits max_tokens and the CLI makes a
    // second API call.  Both responses use the same field id — the continuation
    // chunk must be appended, not discarded.
    const QString response =
        QStringLiteral("===BEGIN 1_text===\n"
                       "First part of the article.\n"
                       "===BEGIN 1_text===\n"
                       "Second part of the article.\n"
                       "===END===\n"
                       "===BEGIN _permalink_slug===\n"
                       "my-article-slug\n"
                       "===END===");
    const auto result = TranslationProtocol::parseResponse(response);
    QCOMPARE(result.size(), 2);
    QCOMPARE(result.value(QStringLiteral("1_text")),
             QStringLiteral("First part of the article.\nSecond part of the article."));
    QCOMPARE(result.value(QStringLiteral("_permalink_slug")), QStringLiteral("my-article-slug"));
}

void Test_TranslationProtocol::test_protocol_parse_continuation_suffix_stripped_and_appended()
{
    // Simulate the _part2 / _continued_part2 / _continued naming the model uses
    // when it hits max_tokens and continues in a second API call.
    // All variants must be stripped to the base field id and appended.
    const QString response =
        QStringLiteral("===BEGIN 1_text===\n"
                       "First part.\n"
                       "===BEGIN 1_text_part2===\n"
                       "Second part.\n"
                       "===END===\n"
                       "===BEGIN 2_title_continued===\n"
                       "Titre traduit.\n"
                       "===END===");
    const auto result = TranslationProtocol::parseResponse(response);
    QCOMPARE(result.size(), 2);
    QCOMPARE(result.value(QStringLiteral("1_text")),
             QStringLiteral("First part.\nSecond part."));
    QCOMPARE(result.value(QStringLiteral("2_title")), QStringLiteral("Titre traduit."));
}

// =============================================================================
// buildPrompt
// =============================================================================

void Test_TranslationProtocol::test_protocol_build_contains_source_and_target_lang()
{
    QList<TranslatableField> fields;
    TranslatableField f;
    f.id         = QStringLiteral("0_title");
    f.sourceText = QStringLiteral("Hello");
    fields.append(f);

    const QString prompt = TranslationProtocol::buildPrompt(fields,
                                                            QStringLiteral("en"),
                                                            QStringLiteral("fr"));
    QVERIFY(prompt.contains(QStringLiteral("en")));
    QVERIFY(prompt.contains(QStringLiteral("fr")));
}

void Test_TranslationProtocol::test_protocol_build_contains_field_id()
{
    QList<TranslatableField> fields;
    TranslatableField f;
    f.id         = QStringLiteral("1_text");
    f.sourceText = QStringLiteral("Some text");
    fields.append(f);

    const QString prompt = TranslationProtocol::buildPrompt(fields,
                                                            QStringLiteral("en"),
                                                            QStringLiteral("de"));
    QVERIFY(prompt.contains(QStringLiteral("1_text")));
}

void Test_TranslationProtocol::test_protocol_build_contains_source_text()
{
    QList<TranslatableField> fields;
    TranslatableField f;
    f.id         = QStringLiteral("0_title");
    f.sourceText = QStringLiteral("My unique source sentence");
    fields.append(f);

    const QString prompt = TranslationProtocol::buildPrompt(fields,
                                                            QStringLiteral("en"),
                                                            QStringLiteral("fr"));
    QVERIFY(prompt.contains(QStringLiteral("My unique source sentence")));
}

void Test_TranslationProtocol::test_protocol_build_multiple_fields()
{
    QList<TranslatableField> fields;
    TranslatableField f1;
    f1.id         = QStringLiteral("0_title");
    f1.sourceText = QStringLiteral("Title text");
    TranslatableField f2;
    f2.id         = QStringLiteral("1_text");
    f2.sourceText = QStringLiteral("Body text");
    fields << f1 << f2;

    const QString prompt = TranslationProtocol::buildPrompt(fields,
                                                            QStringLiteral("en"),
                                                            QStringLiteral("fr"));
    QVERIFY(prompt.contains(QStringLiteral("0_title")));
    QVERIFY(prompt.contains(QStringLiteral("1_text")));
    QVERIFY(prompt.contains(QStringLiteral("Title text")));
    QVERIFY(prompt.contains(QStringLiteral("Body text")));
}

void Test_TranslationProtocol::test_protocol_build_contains_begin_end_format_instructions()
{
    QList<TranslatableField> fields;
    TranslatableField f;
    f.id         = QStringLiteral("0_title");
    f.sourceText = QStringLiteral("Hello");
    fields.append(f);

    const QString prompt = TranslationProtocol::buildPrompt(fields,
                                                            QStringLiteral("en"),
                                                            QStringLiteral("fr"));
    QVERIFY(prompt.contains(QStringLiteral("===BEGIN")));
    QVERIFY(prompt.contains(QStringLiteral("===END===")));
}

// =============================================================================
// round-trip
// =============================================================================

void Test_TranslationProtocol::test_protocol_roundtrip_text_with_embedded_quotes_survives()
{
    // Simulate the article page scenario: source text contains shortcode attrs
    // with double quotes.  Build the prompt, then simulate Claude's response
    // in the delimiter format (as we now instruct it), and parse.
    const QString sourceText =
        QStringLiteral("[TITLE level=\"1\"]My Article Title[/TITLE]\n"
                       "This is a paragraph with a \"quoted word\" inside.\n"
                       "[LINK href=\"https://example.com\"]Click here[/LINK]");

    QList<TranslatableField> fields;
    TranslatableField f;
    f.id         = QStringLiteral("1_text");
    f.sourceText = sourceText;
    fields.append(f);

    // The prompt is built — we trust buildPrompt is correct from other tests.
    // Now simulate what Claude returns in the new format:
    const QString simulatedTranslation =
        QStringLiteral("[TITRE niveau=\"1\"]Mon Article[/TITRE]\n"
                       "Voici un paragraphe avec un \"mot cité\" dedans.\n"
                       "[LIEN href=\"https://example.com\"]Cliquez ici[/LIEN]");

    const QString simulatedResponse =
        QStringLiteral("===BEGIN 1_text===\n") + simulatedTranslation + QStringLiteral("\n===END===");

    const auto result = TranslationProtocol::parseResponse(simulatedResponse);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.value(QStringLiteral("1_text")), simulatedTranslation);
}

// =============================================================================
// sanitizeShortName
// =============================================================================

void Test_TranslationProtocol::test_protocol_sanitize_clean_value_returned_unchanged()
{
    const QString result = TranslationProtocol::sanitizeShortName(
        QStringLiteral("cholesterol-crystal-arthropathy-dos-and-dont"),
        QStringLiteral("cholesterol-crystal-arthropathy-dos-and-dont"));
    QCOMPARE(result, QStringLiteral("cholesterol-crystal-arthropathy-dos-and-dont"));
}

void Test_TranslationProtocol::test_protocol_sanitize_healybio_ko_promptleak_rejected()
{
    // Reproduces the Healybio Korean regression: the AI echoed the slug
    // instruction text verbatim instead of translating.
    const QString raw = QStringLiteral(
        "[This field is a URL slug. Output a valid URL slug only: "
        "lowercase words separated by hyphens, no spaces, no special characters.]");
    const QString result = TranslationProtocol::sanitizeShortName(
        raw, QStringLiteral("hidradenitis-suppurativa-associated-arthropathy-dos-and-dont"));
    QVERIFY(result.isEmpty());
}

void Test_TranslationProtocol::test_protocol_sanitize_healybio_it_duplicate_line_keeps_first()
{
    // Reproduces the Healybio Italian regression: taxonomy.db stored two
    // candidate phrasings joined by a literal newline.
    const QString raw = QStringLiteral(
        "Compromissione della propriocezione degli arti inferiori\n"
        "Alterata propriocezione degli arti inferiori");
    const QString result = TranslationProtocol::sanitizeShortName(
        raw, QStringLiteral("Impaired proprioception in lower limbs"));
    QCOMPARE(result, QStringLiteral("Compromissione della propriocezione degli arti inferiori"));
}

void Test_TranslationProtocol::test_protocol_sanitize_multiline_takes_first_nonempty_line()
{
    const QString result = TranslationProtocol::sanitizeShortName(
        QStringLiteral("First candidate\nSecond candidate\nThird candidate"),
        QStringLiteral("Source name"));
    QCOMPARE(result, QStringLiteral("First candidate"));
}

void Test_TranslationProtocol::test_protocol_sanitize_leading_blank_lines_skipped()
{
    const QString result = TranslationProtocol::sanitizeShortName(
        QStringLiteral("\n\n  Actual value  \nSecond candidate"),
        QStringLiteral("Source name"));
    QCOMPARE(result, QStringLiteral("Actual value"));
}

void Test_TranslationProtocol::test_protocol_sanitize_all_blank_lines_rejected()
{
    const QString result = TranslationProtocol::sanitizeShortName(
        QStringLiteral("\n   \n\t\n"), QStringLiteral("Source name"));
    QVERIFY(result.isEmpty());
}

void Test_TranslationProtocol::test_protocol_sanitize_marker_case_insensitive()
{
    const QString result = TranslationProtocol::sanitizeShortName(
        QStringLiteral("Please output a Valid URL Slug for this topic"),
        QStringLiteral("some-topic"));
    QVERIFY(result.isEmpty());
}

void Test_TranslationProtocol::test_protocol_sanitize_implausibly_long_relative_to_source_rejected()
{
    // Source is short; translated value is unrelated free text far exceeding
    // any plausible name/slug length for that source.
    const QString longGarbage =
        QStringLiteral("this is a very long sentence that goes on and on and is clearly "
                       "not a short name or slug for anything in particular at all");
    const QString result = TranslationProtocol::sanitizeShortName(longGarbage, QStringLiteral("gout"));
    QVERIFY(result.isEmpty());
}

void Test_TranslationProtocol::test_protocol_sanitize_verbose_translation_within_ratio_accepted()
{
    // A verbose but legitimate translation (target language naturally longer
    // than source) must not be rejected as long as it stays within ratio.
    const QString source = QStringLiteral("gout");
    const QString verboseButPlausible = QStringLiteral("goutte-articulaire-chronique");
    const QString result = TranslationProtocol::sanitizeShortName(verboseButPlausible, source);
    QCOMPARE(result, verboseButPlausible);
}

// ---------------------------------------------------------------------------
// normalizeSlug
// ---------------------------------------------------------------------------

void Test_TranslationProtocol::test_protocol_slug_lowercases_and_hyphenates()
{
    QCOMPARE(TranslationProtocol::normalizeSlug(QStringLiteral("Maitriser Le Sommeil"),
                                                 QStringLiteral("master-sleep")),
             QStringLiteral("maitriser-le-sommeil"));
}

void Test_TranslationProtocol::test_protocol_slug_folds_accents_to_ascii()
{
    QCOMPARE(TranslationProtocol::normalizeSlug(QStringLiteral("Santé mentale"),
                                                 QStringLiteral("mental-health")),
             QStringLiteral("sante-mentale"));
}

void Test_TranslationProtocol::test_protocol_slug_strips_invalid_characters()
{
    QCOMPARE(TranslationProtocol::normalizeSlug(QStringLiteral("qu'est-ce que c'est ?"),
                                                 QStringLiteral("what-is-it")),
             QStringLiteral("quest-ce-que-cest"));
}

void Test_TranslationProtocol::test_protocol_slug_collapses_and_trims_hyphens()
{
    QCOMPARE(TranslationProtocol::normalizeSlug(QStringLiteral("--a---b--"),
                                                 QStringLiteral("a-b")),
             QStringLiteral("a-b"));
}

void Test_TranslationProtocol::test_protocol_slug_non_latin_script_returns_empty()
{
    // No ASCII base survives, so the caller keeps the English slug.
    QVERIFY(TranslationProtocol::normalizeSlug(QStringLiteral("マスタースリープ"),
                                                QStringLiteral("master-sleep")).isEmpty());
}

void Test_TranslationProtocol::test_protocol_slug_preserves_html_extension_from_source()
{
    // Regression: '.' is not a legal slug character, so sanitizing in place fused
    // the extension onto the slug and shipped
    // /fr/politique-de-confidentialitehtml for every legal page in every language.
    QCOMPARE(TranslationProtocol::normalizeSlug(
                 QStringLiteral("politique-de-confidentialite.html"),
                 QStringLiteral("privacy-policy.html")),
             QStringLiteral("politique-de-confidentialite.html"));
}

void Test_TranslationProtocol::test_protocol_slug_restores_extension_when_ai_omits_it()
{
    // The extension comes from the source, so it is restored even if the AI
    // answers without one.
    QCOMPARE(TranslationProtocol::normalizeSlug(
                 QStringLiteral("conditions-d-utilisation"),
                 QStringLiteral("terms-of-service.html")),
             QStringLiteral("conditions-d-utilisation.html"));
}

void Test_TranslationProtocol::test_protocol_slug_no_extension_when_source_has_none()
{
    // Ordinary articles must not gain an extension.
    const QString out = TranslationProtocol::normalizeSlug(
        QStringLiteral("polyarthrite-rhumatoide"), QStringLiteral("rheumatoid-arthritis"));
    QCOMPARE(out, QStringLiteral("polyarthrite-rhumatoide"));
    QVERIFY(!out.contains(QLatin1Char('.')));
}

void Test_TranslationProtocol::test_protocol_slug_dotted_source_does_not_fuse_extension()
{
    // The exact production failure: the fused form must never come back.
    const QString out = TranslationProtocol::normalizeSlug(
        QStringLiteral("politique de confidentialité.html"),
        QStringLiteral("privacy-policy.html"));
    QVERIFY2(!out.endsWith(QStringLiteral("html")) || out.endsWith(QStringLiteral(".html")),
             "extension must stay separated by a dot");
    QCOMPARE(out, QStringLiteral("politique-de-confidentialite.html"));
}

QTEST_MAIN(Test_TranslationProtocol)
#include "test_translation_protocol.moc"
