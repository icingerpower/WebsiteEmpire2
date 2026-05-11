#include "LauncherTranslate.h"

#include <QSet>
#include <QTest>
#include <algorithm>

class Test_Website_LauncherTranslate_Modes : public QObject
{
    Q_OBJECT
private slots:
    void test_modes_count_is_three();
    void test_modes_default_is_first();
    void test_modes_text_flag();
    void test_modes_svg_flag();
    void test_modes_flags_unique();
    void test_modes_titles_nonempty();
};

void Test_Website_LauncherTranslate_Modes::test_modes_count_is_three()
{
    QCOMPARE(LauncherTranslate::translateModes().size(), 3);
}

void Test_Website_LauncherTranslate_Modes::test_modes_default_is_first()
{
    const auto &modes = LauncherTranslate::translateModes();
    QVERIFY(!modes.isEmpty());
    QVERIFY(modes.first().flag.isEmpty());
}

void Test_Website_LauncherTranslate_Modes::test_modes_text_flag()
{
    const auto &modes = LauncherTranslate::translateModes();
    const bool found = std::any_of(modes.cbegin(), modes.cend(),
        [](const LauncherTranslate::TranslateMode &m) {
            return m.flag == LauncherTranslate::OPTION_TEXT;
        });
    QVERIFY(found);
}

void Test_Website_LauncherTranslate_Modes::test_modes_svg_flag()
{
    const auto &modes = LauncherTranslate::translateModes();
    const bool found = std::any_of(modes.cbegin(), modes.cend(),
        [](const LauncherTranslate::TranslateMode &m) {
            return m.flag == LauncherTranslate::OPTION_SVG;
        });
    QVERIFY(found);
}

void Test_Website_LauncherTranslate_Modes::test_modes_flags_unique()
{
    QSet<QString> seen;
    for (const auto &m : LauncherTranslate::translateModes()) {
        QVERIFY2(!seen.contains(m.flag),
            qPrintable(QStringLiteral("Duplicate flag: '%1'").arg(m.flag)));
        seen.insert(m.flag);
    }
}

void Test_Website_LauncherTranslate_Modes::test_modes_titles_nonempty()
{
    for (const auto &m : LauncherTranslate::translateModes()) {
        QVERIFY2(!m.title.isEmpty(),
            qPrintable(QStringLiteral("Mode with flag '%1' has empty title").arg(m.flag)));
    }
}

QTEST_MAIN(Test_Website_LauncherTranslate_Modes)
#include "test_launcher_translate_modes.moc"
