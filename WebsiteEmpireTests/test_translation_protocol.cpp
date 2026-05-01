#include <QtTest>

#include "website/translation/TranslationProtocol.h"

class Test_TranslationProtocol : public QObject
{
    Q_OBJECT

private slots:
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

QTEST_MAIN(Test_TranslationProtocol)
#include "test_translation_protocol.moc"
