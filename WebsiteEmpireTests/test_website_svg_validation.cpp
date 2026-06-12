#include <QtTest>
#include <QSvgRenderer>

class Test_Website_SvgValidation : public QObject
{
    Q_OBJECT

private slots:
    void test_svgvalidation_valid_svg_accepted();
    void test_svgvalidation_empty_bytes_rejected();
    void test_svgvalidation_plain_text_rejected();
    void test_svgvalidation_truncated_svg_rejected();
    void test_svgvalidation_non_svg_xml_rejected();
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool svgValid(const QByteArray &data)
{
    QSvgRenderer r;
    return r.load(data);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void Test_Website_SvgValidation::test_svgvalidation_valid_svg_accepted()
{
    const QByteArray svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<text x=\"10\" y=\"20\">Hello</text>"
        "</svg>";
    QVERIFY(svgValid(svg));
}

void Test_Website_SvgValidation::test_svgvalidation_empty_bytes_rejected()
{
    QVERIFY(!svgValid(QByteArray{}));
}

void Test_Website_SvgValidation::test_svgvalidation_plain_text_rejected()
{
    // Qwen sometimes returns a prose explanation instead of SVG.
    const QByteArray plain = "Here is the translated SVG content you requested.";
    QVERIFY(!svgValid(plain));
}

void Test_Website_SvgValidation::test_svgvalidation_truncated_svg_rejected()
{
    // Truncated output — closing tags missing.
    const QByteArray truncated =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<text x=\"10\" y=\"20\">Bonjour";
    QVERIFY(!svgValid(truncated));
}

void Test_Website_SvgValidation::test_svgvalidation_non_svg_xml_rejected()
{
    // Well-formed XML but not SVG — QSvgRenderer should reject it.
    const QByteArray xml = "<?xml version=\"1.0\"?><root><item>value</item></root>";
    QVERIFY(!svgValid(xml));
}

QTEST_MAIN(Test_Website_SvgValidation)
#include "test_website_svg_validation.moc"
