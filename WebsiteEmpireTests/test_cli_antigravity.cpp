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
    void test_antigravity_model_specific_instance_passes_model_flag();
    void test_antigravity_all_clis_contains_model_variants();
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

void Test_Website_CliAntigravity::test_antigravity_model_specific_instance_passes_model_flag()
{
    CliAntigravity defaultCli;
    QCOMPARE(defaultCli.getName(), QStringLiteral("Antigravity"));
    QVERIFY(!defaultCli.promptArgs().contains(QStringLiteral("--model")));

    CliAntigravity flashCli(QStringLiteral("Gemini 3.8 Flash (Medium)"));
    QCOMPARE(flashCli.getName(), QStringLiteral("Antigravity - Gemini 3.8 Flash (Medium)"));
    QCOMPARE(flashCli.getModel(), QStringLiteral("Gemini 3.8 Flash (Medium)"));
    const QStringList args = flashCli.promptArgs();
    const int modelIdx = args.indexOf(QStringLiteral("--model"));
    QVERIFY(modelIdx >= 0);
    QVERIFY(modelIdx + 1 < args.size());
    QCOMPARE(args.at(modelIdx + 1), QStringLiteral("Gemini 3.8 Flash (Medium)"));
}

void Test_Website_CliAntigravity::test_antigravity_all_clis_contains_model_variants()
{
    QStringList names;
    for (const AbstractCli *cli : AbstractCli::ALL_CLIS()) {
        names << cli->getName();
    }
    QVERIFY(names.contains(QStringLiteral("Antigravity")));
    QVERIFY(names.contains(QStringLiteral("Antigravity - Gemini 3.8 Flash (Low)")));
    QVERIFY(names.contains(QStringLiteral("Antigravity - Gemini 3.8 Flash (Medium)")));
    QVERIFY(names.contains(QStringLiteral("Antigravity - Gemini 3.8 Flash (High)")));
    QVERIFY(names.contains(QStringLiteral("Antigravity - Gemini 4 Argon (Low)")));
    QVERIFY(names.contains(QStringLiteral("Antigravity - Gemini 4 Argon (Medium)")));
    QVERIFY(names.contains(QStringLiteral("Antigravity - Gemini 4 Argon (High)")));
    QVERIFY(names.contains(QStringLiteral("Antigravity - Claude Sonnet 5.5 (Medium)")));
    QVERIFY(names.contains(QStringLiteral("Antigravity - Claude Opus 5.5 (Medium)")));
}

QTEST_MAIN(Test_Website_CliAntigravity)
#include "test_cli_antigravity.moc"
