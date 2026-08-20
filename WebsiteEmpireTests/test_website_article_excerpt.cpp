#include <QtTest>

#include "website/pages/blocs/PageBlocArticleUtils.h"

// ---------------------------------------------------------------------------
// Regression guard for ArticleCardUtils::extractExcerpt.
//
// Hub cards call extractExcerpt(body, 4, 200).  Two historical defects made a
// card render the ENTIRE article body:
//
//  1. Only ASCII (. ! ?) and CJK (。！？) terminators were recognised.  Hindi ends
//     sentences with the Devanagari danda "।" — a real article had 225 dandas and
//     zero ". " sequences, so the splitter found no boundary at all.
//  2. The character budget was only enforced on the trailing fragment.  The
//     sentence loop stops *after* appending a sentence that crosses targetChars,
//     so a single long sentence carried the excerpt past the limit unbounded.
//
// Together these shipped multi-thousand-character hub cards on /hi/.
// ---------------------------------------------------------------------------

class Test_Website_ArticleExcerpt : public QObject
{
    Q_OBJECT

private slots:
    // --- sentence terminators per script ---
    void test_excerpt_splits_on_ascii_period();
    void test_excerpt_splits_on_devanagari_danda();
    void test_excerpt_splits_on_cjk_full_stop();
    void test_excerpt_splits_on_urdu_full_stop();

    // --- hard character cap ---
    void test_excerpt_caps_single_long_sentence();
    void test_excerpt_caps_text_with_no_recognised_terminator();
    void test_excerpt_appends_ellipsis_when_capped();
    void test_excerpt_does_not_cut_mid_word_when_capped();

    // --- unchanged behaviour ---
    void test_excerpt_short_text_returned_intact();
    void test_excerpt_empty_input_returns_empty();
    void test_excerpt_strips_shortcodes();
};

namespace {

// Builds a Hindi-style paragraph: sentences terminated by the danda, never by ".".
QString hindiBody(int sentences)
{
    QString out;
    for (int i = 0; i < sentences; ++i) {
        out += QStringLiteral("यह एक हिंदी वाक्य है जो पर्याप्त लंबा है।");
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Terminators
// ---------------------------------------------------------------------------

void Test_Website_ArticleExcerpt::test_excerpt_splits_on_ascii_period()
{
    const QString body = QStringLiteral("One two three. Four five six. Seven eight nine. "
                                         "Ten eleven twelve. Thirteen fourteen fifteen.");
    const QString out = ArticleCardUtils::extractExcerpt(body, 4, 200);
    QVERIFY(!out.isEmpty());
    QVERIFY2(out.size() < body.size(), "excerpt must be shorter than the full body");
}

void Test_Website_ArticleExcerpt::test_excerpt_splits_on_devanagari_danda()
{
    // The exact production shape: many dandas, zero ASCII sentence breaks.
    const QString body = hindiBody(40);
    QVERIFY2(!body.contains(QStringLiteral(". ")), "fixture must contain no ASCII breaks");

    const QString out = ArticleCardUtils::extractExcerpt(body, 4, 200);
    QVERIFY(!out.isEmpty());
    QVERIFY2(out.size() < body.size(),
             "danda must be recognised — otherwise the whole body is returned");
    QVERIFY2(out.size() <= 401, "excerpt must respect the character budget");
}

void Test_Website_ArticleExcerpt::test_excerpt_splits_on_cjk_full_stop()
{
    QString body;
    for (int i = 0; i < 40; ++i) {
        body += QStringLiteral("これは十分に長い日本語の文章です。");
    }
    const QString out = ArticleCardUtils::extractExcerpt(body, 4, 200);
    QVERIFY(!out.isEmpty());
    QVERIFY(out.size() < body.size());
}

void Test_Website_ArticleExcerpt::test_excerpt_splits_on_urdu_full_stop()
{
    QString body;
    for (int i = 0; i < 40; ++i) {
        body += QStringLiteral("یہ ایک اردو جملہ ہے جو کافی لمبا ہے۔");
    }
    const QString out = ArticleCardUtils::extractExcerpt(body, 4, 200);
    QVERIFY(!out.isEmpty());
    QVERIFY(out.size() < body.size());
}

// ---------------------------------------------------------------------------
// Hard cap
// ---------------------------------------------------------------------------

void Test_Website_ArticleExcerpt::test_excerpt_caps_single_long_sentence()
{
    // One sentence far longer than targetChars: the loop appends it whole, so the
    // cap is the only thing standing between this and a full-body card.
    QString body;
    for (int i = 0; i < 200; ++i) {
        body += QStringLiteral("word ");
    }
    body += QStringLiteral("end.");
    const QString out = ArticleCardUtils::extractExcerpt(body, 4, 200);
    QVERIFY2(out.size() <= 401, QStringLiteral("excerpt was %1 chars").arg(out.size()).toUtf8());
}

void Test_Website_ArticleExcerpt::test_excerpt_caps_text_with_no_recognised_terminator()
{
    // No terminator of any script — worst case; must still be bounded.
    QString body;
    for (int i = 0; i < 300; ++i) {
        body += QStringLiteral("filler ");
    }
    const QString out = ArticleCardUtils::extractExcerpt(body, 4, 200);
    QVERIFY2(out.size() <= 401, QStringLiteral("excerpt was %1 chars").arg(out.size()).toUtf8());
}

void Test_Website_ArticleExcerpt::test_excerpt_appends_ellipsis_when_capped()
{
    QString body;
    for (int i = 0; i < 300; ++i) {
        body += QStringLiteral("filler ");
    }
    const QString out = ArticleCardUtils::extractExcerpt(body, 4, 200);
    QVERIFY2(out.endsWith(QStringLiteral("…")), "a truncated excerpt must signal truncation");
}

void Test_Website_ArticleExcerpt::test_excerpt_does_not_cut_mid_word_when_capped()
{
    QString body;
    for (int i = 0; i < 300; ++i) {
        body += QStringLiteral("abcdefgh ");
    }
    const QString out = ArticleCardUtils::extractExcerpt(body, 4, 200);
    QString withoutEllipsis = out;
    if (withoutEllipsis.endsWith(QStringLiteral("…"))) {
        withoutEllipsis.chop(1);
    }
    // Every complete token is 8 chars; a mid-word cut would leave a shorter tail.
    const QStringList tokens = withoutEllipsis.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QVERIFY(!tokens.isEmpty());
    QCOMPARE(tokens.last().size(), 8);
}

// ---------------------------------------------------------------------------
// Unchanged behaviour
// ---------------------------------------------------------------------------

void Test_Website_ArticleExcerpt::test_excerpt_short_text_returned_intact()
{
    const QString body = QStringLiteral("A short intro sentence. And a second one.");
    const QString out = ArticleCardUtils::extractExcerpt(body, 4, 200);
    QVERIFY(!out.endsWith(QStringLiteral("…")));
    QVERIFY(out.contains(QStringLiteral("short intro")));
    QVERIFY(out.contains(QStringLiteral("second one")));
}

void Test_Website_ArticleExcerpt::test_excerpt_empty_input_returns_empty()
{
    QVERIFY(ArticleCardUtils::extractExcerpt(QString(), 4, 200).isEmpty());
}

void Test_Website_ArticleExcerpt::test_excerpt_strips_shortcodes()
{
    const QString body = QStringLiteral("[TITLE level=\"1\"]Heading[/TITLE]\n\n"
                                         "Real body text starts here. Second sentence.");
    const QString out = ArticleCardUtils::extractExcerpt(body, 4, 200);
    QVERIFY2(!out.contains(QLatin1Char('[')), "shortcode markup must not leak into the card");
    QVERIFY(out.contains(QStringLiteral("Real body text")));
}

QTEST_MAIN(Test_Website_ArticleExcerpt)
#include "test_website_article_excerpt.moc"
