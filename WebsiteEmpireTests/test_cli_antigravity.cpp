#include <QtTest>

#include "aicli/AbstractCli.h"
#include "aicli/CliAntigravity.h"

class Test_Website_CliAntigravity : public QObject
{
    Q_OBJECT

private slots:
    // Regression: preparePrompt() used to match the substring "Image filename",
    // which the SVG prompt also contains, injecting a contradictory
    // "use generate_image and write a file" preamble into every SVG call.
    void test_antigravity_svg_prompt_not_treated_as_raster();
    void test_antigravity_raster_marker_triggers_generate_image_directive();
    void test_antigravity_plain_text_prompt_gets_text_only_guard();
};

void Test_Website_CliAntigravity::test_antigravity_svg_prompt_not_treated_as_raster()
{
    CliAntigravity cli;
    const QString svgPrompt = QStringLiteral(
        "Create a standalone SVG image for the following web article.\n\n"
        "Image id          : hero\n"
        "Image filename    : hero.svg\n"
        "Image description : test\n\n"
        "Do NOT use any Write, Edit, or file-creation tools — output only as text.\n");

    const QString prepared = cli.preparePrompt(svgPrompt);

    QVERIFY(!prepared.contains(QStringLiteral("generate_image")));
}

void Test_Website_CliAntigravity::test_antigravity_raster_marker_triggers_generate_image_directive()
{
    CliAntigravity cli;
    const QString rasterPrompt = QStringLiteral(
        "Create a photorealistic raster image for the following web article.\n\n"
        "Task type         : RASTER_IMAGE_GENERATION\n"
        "Image id          : idea-1\n"
        "Image description : burgundy dress with nude heels\n");

    const QString prepared = cli.preparePrompt(rasterPrompt);

    QVERIFY(prepared.contains(QStringLiteral("generate_image")));
}

void Test_Website_CliAntigravity::test_antigravity_plain_text_prompt_gets_text_only_guard()
{
    CliAntigravity cli;
    const QString textPrompt = QStringLiteral("Write an article about burgundy dresses.");

    const QString prepared = cli.preparePrompt(textPrompt);

    QVERIFY(!prepared.contains(QStringLiteral("generate_image")));
    QVERIFY(prepared.contains(QStringLiteral("plain text")));
}

QTEST_MAIN(Test_Website_CliAntigravity)
#include "test_cli_antigravity.moc"
