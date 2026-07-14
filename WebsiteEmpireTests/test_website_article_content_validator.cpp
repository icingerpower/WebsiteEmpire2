#include <QtTest>

#include "website/ArticleContentValidator.h"

// =============================================================================
// Test_Website_ArticleContentValidator
// =============================================================================

class Test_Website_ArticleContentValidator : public QObject
{
    Q_OBJECT

private slots:
    // --- hasMarkdownArtifact(): Markdown table detection ---
    void test_articlevalidator_hasmarkdownartifact_table_separator_detected();
    void test_articlevalidator_hasmarkdownartifact_table_separator_no_spaces_detected();
    void test_articlevalidator_hasmarkdownartifact_plain_prose_not_detected();

    // --- hasMarkdownArtifact(): literal [NEWLINE] token detection ---
    void test_articlevalidator_hasmarkdownartifact_newline_token_detected();
    void test_articlevalidator_hasmarkdownartifact_newline_token_case_insensitive();

    // --- isValid(): the exact real-world regression ---
    void test_articlevalidator_isvalid_rejects_healybio_osteoarthritis_regression();

    // --- isValid(): pre-existing checks still enforced ---
    void test_articlevalidator_isvalid_accepts_clean_article();
    void test_articlevalidator_isvalid_rejects_missing_title();
    void test_articlevalidator_isvalid_rejects_too_short();
    void test_articlevalidator_isvalid_rejects_malformed_shortcode_argument();
};

// =============================================================================
// hasMarkdownArtifact(): Markdown table detection
// =============================================================================

void Test_Website_ArticleContentValidator::test_articlevalidator_hasmarkdownartifact_table_separator_detected()
{
    const QString text = QStringLiteral(
        "| Category | Summary | | --- | --- | | Foo | Bar |");
    QVERIFY(ArticleContentValidator::hasMarkdownArtifact(text));
}

void Test_Website_ArticleContentValidator::test_articlevalidator_hasmarkdownartifact_table_separator_no_spaces_detected()
{
    const QString text = QStringLiteral("|Category|Summary|\n|---|---|\n|Foo|Bar|");
    QVERIFY(ArticleContentValidator::hasMarkdownArtifact(text));
}

void Test_Website_ArticleContentValidator::test_articlevalidator_hasmarkdownartifact_plain_prose_not_detected()
{
    const QString text = QStringLiteral(
        "This article discusses joint pain. Cost was $10 | $20 depending on region.");
    QVERIFY(!ArticleContentValidator::hasMarkdownArtifact(text));
}

// =============================================================================
// hasMarkdownArtifact(): literal [NEWLINE] token detection
// =============================================================================

void Test_Website_ArticleContentValidator::test_articlevalidator_hasmarkdownartifact_newline_token_detected()
{
    const QString text = QStringLiteral(
        "1. Do the first thing.[NEWLINE]2. Do the second thing.");
    QVERIFY(ArticleContentValidator::hasMarkdownArtifact(text));
}

void Test_Website_ArticleContentValidator::test_articlevalidator_hasmarkdownartifact_newline_token_case_insensitive()
{
    const QString text = QStringLiteral("First line.[newline]Second line.");
    QVERIFY(ArticleContentValidator::hasMarkdownArtifact(text));
}

// =============================================================================
// isValid(): the exact real-world regression
// =============================================================================

void Test_Website_ArticleContentValidator::test_articlevalidator_isvalid_rejects_healybio_osteoarthritis_regression()
{
    // Reproduces the corrupted article published to Healybio: Antigravity
    // wrote a raw Markdown summary table with literal "[NEWLINE]" tokens
    // instead of using shortcodes, and it leaked straight into the rendered
    // HTML because no known shortcode syntax was involved.
    QString body = QStringLiteral("[TITLE level=\"1\"]Osteoarthritis: A Practical Guide[/TITLE]");
    body += QString(2100, QLatin1Char('x')); // pad past the 2000-char minimum
    body += QStringLiteral(
        "[TITLE level=\"2\"]The 60-Second Plan[/TITLE]"
        "| Category | Summary of Recommendations | | --- | --- | "
        "| What Osteoarthritis Is | A chronic, structural joint condition. | "
        "| Top 3 Dos | 1. Engage in low-impact exercise.[NEWLINE]2. Wear supportive footwear."
        "[NEWLINE]3. Use heat therapy. | | Best Next Step | Consult a physician. |");

    QVERIFY(!ArticleContentValidator::isValid(body));
}

// =============================================================================
// isValid(): pre-existing checks still enforced
// =============================================================================

void Test_Website_ArticleContentValidator::test_articlevalidator_isvalid_accepts_clean_article()
{
    QString body = QStringLiteral("[TITLE level=\"1\"]A Clean Article[/TITLE]");
    body += QString(2100, QLatin1Char('x'));
    body += QStringLiteral("[TITLE level=\"2\"]Section[/TITLE]Plain prose content here.");

    QVERIFY(ArticleContentValidator::isValid(body));
}

void Test_Website_ArticleContentValidator::test_articlevalidator_isvalid_rejects_missing_title()
{
    QString body = QStringLiteral("No title shortcode here, just text. ");
    body += QString(2100, QLatin1Char('x'));

    QVERIFY(!ArticleContentValidator::isValid(body));
}

void Test_Website_ArticleContentValidator::test_articlevalidator_isvalid_rejects_too_short()
{
    const QString body = QStringLiteral("[TITLE level=\"1\"]Short[/TITLE]Too short.");
    QVERIFY(!ArticleContentValidator::isValid(body));
}

void Test_Website_ArticleContentValidator::test_articlevalidator_isvalid_rejects_malformed_shortcode_argument()
{
    QString body = QStringLiteral("[TITLE level=\"1\"]A Title[/TITLE]");
    body += QString(2100, QLatin1Char('x'));
    // silen is not a known argument for TITLE.
    body += QStringLiteral("[TITLE level=\"2\" silen=\"true\"]Section[/TITLE]");

    QVERIFY(!ArticleContentValidator::isValid(body));
}

QTEST_MAIN(Test_Website_ArticleContentValidator)
#include "test_website_article_content_validator.moc"
