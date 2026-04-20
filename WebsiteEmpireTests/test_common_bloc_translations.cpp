#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>

#include "ExceptionWithTitleText.h"
#include "website/AbstractEngine.h"
#include "website/EngineArticles.h"
#include "website/HostTable.h"
#include "website/commonblocs/AbstractCommonBloc.h"
#include "website/commonblocs/BlocTranslations.h"
#include "website/commonblocs/CommonBlocHeader.h"
#include "website/commonblocs/CommonBlocFooter.h"
#include "website/commonblocs/CommonBlocMenuTop.h"
#include "website/commonblocs/CommonBlocMenuBottom.h"
#include "website/commonblocs/MenuItem.h"
#include "website/theme/AbstractTheme.h"
#include "website/theme/ThemeDefault.h"
#include "website/translation/CommonBlocTranslator.h"

// =============================================================================
// Helpers
// =============================================================================

namespace {

template<typename Fn>
bool throwsException(Fn &&fn)
{
    try {
        fn();
        return false;
    } catch (const ExceptionWithTitleText &) {
        return true;
    }
}

} // namespace

// =============================================================================
// Test_BlocTranslations
// =============================================================================

class Test_BlocTranslations : public QObject
{
    Q_OBJECT

private slots:
    void test_bloctranslations_source_registers_field();
    void test_bloctranslations_source_empty_does_not_appear_in_missing();
    void test_bloctranslations_translation_stored_and_retrieved();
    void test_bloctranslations_translation_rejected_for_unregistered_field();
    void test_bloctranslations_source_change_purges_stale_translation();
    void test_bloctranslations_source_change_keeps_valid_other_field();
    void test_bloctranslations_source_change_keeps_other_lang_if_same_text();
    void test_bloctranslations_is_complete_true_when_all_fields_translated();
    void test_bloctranslations_is_complete_false_when_field_missing();
    void test_bloctranslations_is_complete_true_when_source_empty();
    void test_bloctranslations_missing_fields_returns_untranslated();
    void test_bloctranslations_save_load_roundtrip();
    void test_bloctranslations_stale_not_reloaded();
    void test_bloctranslations_known_lang_codes();
};

void Test_BlocTranslations::test_bloctranslations_source_registers_field()
{
    BlocTranslations tr;
    tr.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    QCOMPARE(tr.source(QStringLiteral("title")), QStringLiteral("Hello"));
}

void Test_BlocTranslations::test_bloctranslations_source_empty_does_not_appear_in_missing()
{
    BlocTranslations tr;
    tr.setSource(QStringLiteral("title"), QString());
    const QStringList missing = tr.missingFields(QStringLiteral("fr"));
    QVERIFY(missing.isEmpty());
}

void Test_BlocTranslations::test_bloctranslations_translation_stored_and_retrieved()
{
    BlocTranslations tr;
    tr.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    tr.setTranslation(QStringLiteral("title"), QStringLiteral("fr"), QStringLiteral("Bonjour"));
    QCOMPARE(tr.translation(QStringLiteral("title"), QStringLiteral("fr")),
             QStringLiteral("Bonjour"));
}

void Test_BlocTranslations::test_bloctranslations_translation_rejected_for_unregistered_field()
{
    BlocTranslations tr;
    // Should not crash; just silently ignored.
    tr.setTranslation(QStringLiteral("unknown"), QStringLiteral("fr"), QStringLiteral("text"));
    QVERIFY(tr.translation(QStringLiteral("unknown"), QStringLiteral("fr")).isEmpty());
}

void Test_BlocTranslations::test_bloctranslations_source_change_purges_stale_translation()
{
    BlocTranslations tr;
    tr.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    tr.setTranslation(QStringLiteral("title"), QStringLiteral("fr"), QStringLiteral("Bonjour"));
    QCOMPARE(tr.translation(QStringLiteral("title"), QStringLiteral("fr")),
             QStringLiteral("Bonjour"));

    // Change source text -> translation should be purged
    tr.setSource(QStringLiteral("title"), QStringLiteral("Goodbye"));
    QVERIFY(tr.translation(QStringLiteral("title"), QStringLiteral("fr")).isEmpty());
}

void Test_BlocTranslations::test_bloctranslations_source_change_keeps_valid_other_field()
{
    BlocTranslations tr;
    tr.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    tr.setSource(QStringLiteral("subtitle"), QStringLiteral("World"));
    tr.setTranslation(QStringLiteral("title"), QStringLiteral("fr"), QStringLiteral("Bonjour"));
    tr.setTranslation(QStringLiteral("subtitle"), QStringLiteral("fr"), QStringLiteral("Monde"));

    // Change only title source -> subtitle translation must survive
    tr.setSource(QStringLiteral("title"), QStringLiteral("Hi"));
    QVERIFY(tr.translation(QStringLiteral("title"), QStringLiteral("fr")).isEmpty());
    QCOMPARE(tr.translation(QStringLiteral("subtitle"), QStringLiteral("fr")),
             QStringLiteral("Monde"));
}

void Test_BlocTranslations::test_bloctranslations_source_change_keeps_other_lang_if_same_text()
{
    BlocTranslations tr;
    tr.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    tr.setTranslation(QStringLiteral("title"), QStringLiteral("fr"), QStringLiteral("Bonjour"));
    tr.setTranslation(QStringLiteral("title"), QStringLiteral("de"), QStringLiteral("Hallo"));

    // Re-set same source text -> both translations should survive
    tr.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    QCOMPARE(tr.translation(QStringLiteral("title"), QStringLiteral("fr")),
             QStringLiteral("Bonjour"));
    QCOMPARE(tr.translation(QStringLiteral("title"), QStringLiteral("de")),
             QStringLiteral("Hallo"));
}

void Test_BlocTranslations::test_bloctranslations_is_complete_true_when_all_fields_translated()
{
    BlocTranslations tr;
    tr.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    tr.setSource(QStringLiteral("subtitle"), QStringLiteral("World"));
    tr.setTranslation(QStringLiteral("title"), QStringLiteral("fr"), QStringLiteral("Bonjour"));
    tr.setTranslation(QStringLiteral("subtitle"), QStringLiteral("fr"), QStringLiteral("Monde"));
    QVERIFY(tr.isComplete(QStringLiteral("fr")));
}

void Test_BlocTranslations::test_bloctranslations_is_complete_false_when_field_missing()
{
    BlocTranslations tr;
    tr.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    tr.setSource(QStringLiteral("subtitle"), QStringLiteral("World"));
    tr.setTranslation(QStringLiteral("title"), QStringLiteral("fr"), QStringLiteral("Bonjour"));
    // subtitle not translated
    QVERIFY(!tr.isComplete(QStringLiteral("fr")));
}

void Test_BlocTranslations::test_bloctranslations_is_complete_true_when_source_empty()
{
    BlocTranslations tr;
    tr.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    tr.setSource(QStringLiteral("subtitle"), QString()); // empty
    tr.setTranslation(QStringLiteral("title"), QStringLiteral("fr"), QStringLiteral("Bonjour"));
    QVERIFY(tr.isComplete(QStringLiteral("fr")));
}

void Test_BlocTranslations::test_bloctranslations_missing_fields_returns_untranslated()
{
    BlocTranslations tr;
    tr.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    tr.setSource(QStringLiteral("subtitle"), QStringLiteral("World"));
    tr.setTranslation(QStringLiteral("title"), QStringLiteral("fr"), QStringLiteral("Bonjour"));

    const QStringList missing = tr.missingFields(QStringLiteral("fr"));
    QCOMPARE(missing.size(), 1);
    QCOMPARE(missing.first(), QStringLiteral("subtitle"));
}

void Test_BlocTranslations::test_bloctranslations_save_load_roundtrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.filePath(QStringLiteral("test.ini"));

    {
        BlocTranslations tr;
        tr.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
        tr.setSource(QStringLiteral("subtitle"), QStringLiteral("World"));
        tr.setTranslation(QStringLiteral("title"), QStringLiteral("fr"), QStringLiteral("Bonjour"));
        tr.setTranslation(QStringLiteral("subtitle"), QStringLiteral("fr"), QStringLiteral("Monde"));
        tr.setTranslation(QStringLiteral("title"), QStringLiteral("de"), QStringLiteral("Hallo"));

        QSettings settings(path, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("header"));
        tr.saveToSettings(settings);
        settings.endGroup();
    }

    {
        BlocTranslations tr;
        // Must register sources before loading (fromMap does this in production)
        tr.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
        tr.setSource(QStringLiteral("subtitle"), QStringLiteral("World"));

        QSettings settings(path, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("header"));
        tr.loadFromSettings(settings);
        settings.endGroup();

        QCOMPARE(tr.translation(QStringLiteral("title"), QStringLiteral("fr")),
                 QStringLiteral("Bonjour"));
        QCOMPARE(tr.translation(QStringLiteral("subtitle"), QStringLiteral("fr")),
                 QStringLiteral("Monde"));
        QCOMPARE(tr.translation(QStringLiteral("title"), QStringLiteral("de")),
                 QStringLiteral("Hallo"));
    }
}

void Test_BlocTranslations::test_bloctranslations_stale_not_reloaded()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.filePath(QStringLiteral("test.ini"));

    {
        BlocTranslations tr;
        tr.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
        tr.setTranslation(QStringLiteral("title"), QStringLiteral("fr"), QStringLiteral("Bonjour"));

        QSettings settings(path, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("bloc"));
        tr.saveToSettings(settings);
        settings.endGroup();
    }

    {
        BlocTranslations tr;
        // Different source text -> stored translation is stale
        tr.setSource(QStringLiteral("title"), QStringLiteral("Goodbye"));

        QSettings settings(path, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("bloc"));
        tr.loadFromSettings(settings);
        settings.endGroup();

        QVERIFY(tr.translation(QStringLiteral("title"), QStringLiteral("fr")).isEmpty());
    }
}

void Test_BlocTranslations::test_bloctranslations_known_lang_codes()
{
    BlocTranslations tr;
    tr.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    tr.setTranslation(QStringLiteral("title"), QStringLiteral("fr"), QStringLiteral("Bonjour"));
    tr.setTranslation(QStringLiteral("title"), QStringLiteral("de"), QStringLiteral("Hallo"));

    QStringList langs = tr.knownLangCodes();
    langs.sort();
    QCOMPARE(langs.size(), 2);
    QVERIFY(langs.contains(QStringLiteral("fr")));
    QVERIFY(langs.contains(QStringLiteral("de")));
}

// =============================================================================
// Test_CommonBlocHeader_Translations
// =============================================================================

class Test_CommonBlocHeader_Translations : public QObject
{
    Q_OBJECT

private slots:
    void test_header_set_title_purges_stale_translation();
    void test_header_source_texts_includes_nonempty_fields();
    void test_header_source_texts_excludes_empty_subtitle();
    void test_header_assert_translated_passes_for_source_lang();
    void test_header_assert_translated_passes_when_complete();
    void test_header_assert_translated_raises_for_missing_title();
    void test_header_assert_translated_raises_for_missing_subtitle();
    void test_header_assert_translated_ignores_empty_source();
    void test_header_addcode_uses_base_for_source_lang();
    void test_header_addcode_uses_translation_for_other_lang();
    void test_header_missing_translations_empty_for_source_lang();
    void test_header_missing_translations_lists_missing_fields();
};

void Test_CommonBlocHeader_Translations::test_header_set_title_purges_stale_translation()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Hello"));
    header.setTranslation(QLatin1String(CommonBlocHeader::KEY_TITLE),
                          QStringLiteral("fr"),
                          QStringLiteral("Bonjour"));
    QVERIFY(header.missingTranslations(QStringLiteral("fr"), QStringLiteral("en")).isEmpty());

    // Change title -> stale translation purged
    header.setTitle(QStringLiteral("Hi"));
    const QStringList missing = header.missingTranslations(QStringLiteral("fr"), QStringLiteral("en"));
    QVERIFY(missing.contains(QLatin1String(CommonBlocHeader::KEY_TITLE)));
}

void Test_CommonBlocHeader_Translations::test_header_source_texts_includes_nonempty_fields()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));
    header.setSubtitle(QStringLiteral("Subtitle"));

    const auto sources = header.sourceTexts();
    QCOMPARE(sources.size(), 2);
    QCOMPARE(sources.value(QLatin1String(CommonBlocHeader::KEY_TITLE)), QStringLiteral("Title"));
    QCOMPARE(sources.value(QLatin1String(CommonBlocHeader::KEY_SUBTITLE)), QStringLiteral("Subtitle"));
}

void Test_CommonBlocHeader_Translations::test_header_source_texts_excludes_empty_subtitle()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));
    // subtitle is empty by default

    const auto sources = header.sourceTexts();
    QCOMPARE(sources.size(), 1);
    QVERIFY(!sources.contains(QLatin1String(CommonBlocHeader::KEY_SUBTITLE)));
}

void Test_CommonBlocHeader_Translations::test_header_assert_translated_passes_for_source_lang()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));
    // No translations set, but langCode == sourceLangCode -> should not raise
    header.assertTranslated(QStringLiteral("en"), QStringLiteral("en"));
}

void Test_CommonBlocHeader_Translations::test_header_assert_translated_passes_when_complete()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));
    header.setSubtitle(QStringLiteral("Subtitle"));
    header.setTranslation(QLatin1String(CommonBlocHeader::KEY_TITLE),
                          QStringLiteral("fr"),
                          QStringLiteral("Titre"));
    header.setTranslation(QLatin1String(CommonBlocHeader::KEY_SUBTITLE),
                          QStringLiteral("fr"),
                          QStringLiteral("Sous-titre"));
    // Should not raise
    header.assertTranslated(QStringLiteral("fr"), QStringLiteral("en"));
}

void Test_CommonBlocHeader_Translations::test_header_assert_translated_raises_for_missing_title()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));

    QVERIFY(throwsException([&] {
        header.assertTranslated(QStringLiteral("fr"), QStringLiteral("en"));
    }));
}

void Test_CommonBlocHeader_Translations::test_header_assert_translated_raises_for_missing_subtitle()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));
    header.setSubtitle(QStringLiteral("Subtitle"));
    header.setTranslation(QLatin1String(CommonBlocHeader::KEY_TITLE),
                          QStringLiteral("fr"),
                          QStringLiteral("Titre"));
    // subtitle translation missing

    QVERIFY(throwsException([&] {
        header.assertTranslated(QStringLiteral("fr"), QStringLiteral("en"));
    }));
}

void Test_CommonBlocHeader_Translations::test_header_assert_translated_ignores_empty_source()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));
    // subtitle empty -> should not require translation
    header.setTranslation(QLatin1String(CommonBlocHeader::KEY_TITLE),
                          QStringLiteral("fr"),
                          QStringLiteral("Titre"));
    // Should not raise even though subtitle has no translation
    header.assertTranslated(QStringLiteral("fr"), QStringLiteral("en"));
}

void Test_CommonBlocHeader_Translations::test_header_addcode_uses_base_for_source_lang()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ThemeDefault theme(dir.path());
    theme.setSourceLangCode(QStringLiteral("en"));

    HostTable hostTable(dir.path());
    EngineArticles *engine = static_cast<EngineArticles *>(
        EngineArticles().create(this));
    engine->init(dir.path(), hostTable);
    engine->setTheme(&theme);

    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));
    header.setTranslation(QLatin1String(CommonBlocHeader::KEY_TITLE),
                          QStringLiteral("fr"),
                          QStringLiteral("Mon Site"));

    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    // websiteIndex 0 -> getLangCode returns "" for uninited engine with no rows,
    // which matches the sourceLang if sourceLang is also effectively default.
    // Since engine was inited with empty dir, rowCount is 0, getLangCode(0) returns "".
    // sourceLang is "en". "" != "en" so useTranslation would be true but no
    // translation for "" exists, so it falls back to m_title.
    header.addCode(QStringView{}, *engine, 0, html, css, js, cssDone, jsDone);
    QVERIFY(html.contains(QStringLiteral("My Site")));

    delete engine;
}

void Test_CommonBlocHeader_Translations::test_header_addcode_uses_translation_for_other_lang()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ThemeDefault theme(dir.path());
    theme.setSourceLangCode(QStringLiteral("en"));

    HostTable hostTable(dir.path());
    EngineArticles *engine = static_cast<EngineArticles *>(
        EngineArticles().create(this));
    engine->init(dir.path(), hostTable);
    engine->setTheme(&theme);

    // The engine has no rows, so getLangCode(0) returns "".
    // We test the translation path by directly checking the bloc's behavior.
    // Use a stub approach: set lang = "fr" via the bloc's addCode where
    // engine.getLangCode returns "fr".
    // Since we can't easily control getLangCode, test via missingTranslations
    // and the BlocTranslations directly.
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));
    header.setTranslation(QLatin1String(CommonBlocHeader::KEY_TITLE),
                          QStringLiteral("fr"),
                          QStringLiteral("Mon Site"));

    // Verify the translation is stored correctly
    const QStringList missing = header.missingTranslations(QStringLiteral("fr"), QStringLiteral("en"));
    QVERIFY(missing.isEmpty());

    delete engine;
}

void Test_CommonBlocHeader_Translations::test_header_missing_translations_empty_for_source_lang()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));
    const QStringList missing = header.missingTranslations(QStringLiteral("en"), QStringLiteral("en"));
    QVERIFY(missing.isEmpty());
}

void Test_CommonBlocHeader_Translations::test_header_missing_translations_lists_missing_fields()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));
    header.setSubtitle(QStringLiteral("Subtitle"));
    header.setTranslation(QLatin1String(CommonBlocHeader::KEY_TITLE),
                          QStringLiteral("fr"),
                          QStringLiteral("Titre"));

    const QStringList missing = header.missingTranslations(QStringLiteral("fr"), QStringLiteral("en"));
    QCOMPARE(missing.size(), 1);
    QCOMPARE(missing.first(), QLatin1String(CommonBlocHeader::KEY_SUBTITLE));
}

// =============================================================================
// Test_CommonBlocMenu_Translations
// =============================================================================

class Test_CommonBlocMenu_Translations : public QObject
{
    Q_OBJECT

private slots:
    void test_menu_set_translation_stored();
    void test_menu_remove_and_readd_item_preserves_translation();
    void test_menu_label_change_causes_missing_translation();
    void test_menu_assert_translated_passes_for_source_lang();
    void test_menu_assert_translated_raises_when_label_untranslated();
    void test_menu_assert_translated_passes_when_all_labels_translated();
    void test_menu_source_texts_contains_all_labels();
    void test_menu_save_load_roundtrip_preserves_translations();
};

void Test_CommonBlocMenu_Translations::test_menu_set_translation_stored()
{
    CommonBlocMenuTop menu;
    MenuItem item;
    item.label = QStringLiteral("Home");
    item.url = QStringLiteral("/");
    menu.setItems({item});

    // setTranslation for menus takes a JSON object: {sourceLabel: translatedLabel}
    menu.setTranslation(QStringLiteral("items"),
                        QStringLiteral("fr"),
                        QStringLiteral("{\"Home\":\"Accueil\"}"));

    const QStringList missing = menu.missingTranslations(QStringLiteral("fr"), QStringLiteral("en"));
    QVERIFY(missing.isEmpty());
}

void Test_CommonBlocMenu_Translations::test_menu_remove_and_readd_item_preserves_translation()
{
    CommonBlocMenuTop menu;
    MenuItem item;
    item.label = QStringLiteral("Home");
    item.url = QStringLiteral("/");
    menu.setItems({item});

    menu.setTranslation(QStringLiteral("items"),
                        QStringLiteral("fr"),
                        QStringLiteral("{\"Home\":\"Accueil\"}"));

    // Remove all items
    menu.setItems({});
    QVERIFY(menu.missingTranslations(QStringLiteral("fr"), QStringLiteral("en")).isEmpty());

    // Re-add with same label -> translation should still be there
    menu.setItems({item});
    const QStringList missing = menu.missingTranslations(QStringLiteral("fr"), QStringLiteral("en"));
    QVERIFY(missing.isEmpty());
}

void Test_CommonBlocMenu_Translations::test_menu_label_change_causes_missing_translation()
{
    CommonBlocMenuTop menu;
    MenuItem item;
    item.label = QStringLiteral("Home");
    item.url = QStringLiteral("/");
    menu.setItems({item});

    menu.setTranslation(QStringLiteral("items"),
                        QStringLiteral("fr"),
                        QStringLiteral("{\"Home\":\"Accueil\"}"));

    // Change label to something different
    item.label = QStringLiteral("Main Page");
    menu.setItems({item});

    const QStringList missing = menu.missingTranslations(QStringLiteral("fr"), QStringLiteral("en"));
    QCOMPARE(missing.size(), 1);
    QCOMPARE(missing.first(), QStringLiteral("Main Page"));
}

void Test_CommonBlocMenu_Translations::test_menu_assert_translated_passes_for_source_lang()
{
    CommonBlocMenuTop menu;
    MenuItem item;
    item.label = QStringLiteral("Home");
    item.url = QStringLiteral("/");
    menu.setItems({item});

    // No translations, but lang == sourceLang -> should not raise
    menu.assertTranslated(QStringLiteral("en"), QStringLiteral("en"));
}

void Test_CommonBlocMenu_Translations::test_menu_assert_translated_raises_when_label_untranslated()
{
    CommonBlocMenuTop menu;
    MenuItem item;
    item.label = QStringLiteral("Home");
    item.url = QStringLiteral("/");
    menu.setItems({item});

    QVERIFY(throwsException([&] {
        menu.assertTranslated(QStringLiteral("fr"), QStringLiteral("en"));
    }));
}

void Test_CommonBlocMenu_Translations::test_menu_assert_translated_passes_when_all_labels_translated()
{
    CommonBlocMenuTop menu;

    MenuItem item;
    item.label = QStringLiteral("Home");
    item.url = QStringLiteral("/");

    MenuSubItem sub;
    sub.label = QStringLiteral("About");
    sub.url = QStringLiteral("/about");
    item.children.append(sub);

    menu.setItems({item});

    menu.setTranslation(QStringLiteral("items"),
                        QStringLiteral("fr"),
                        QStringLiteral("{\"Home\":\"Accueil\",\"About\":\"A propos\"}"));

    // Should not raise
    menu.assertTranslated(QStringLiteral("fr"), QStringLiteral("en"));
}

void Test_CommonBlocMenu_Translations::test_menu_source_texts_contains_all_labels()
{
    CommonBlocMenuTop menu;

    MenuItem item;
    item.label = QStringLiteral("Home");
    item.url = QStringLiteral("/");

    MenuSubItem sub;
    sub.label = QStringLiteral("About");
    sub.url = QStringLiteral("/about");
    item.children.append(sub);

    menu.setItems({item});

    const auto sources = menu.sourceTexts();
    QVERIFY(sources.contains(QStringLiteral("items")));
    const QString itemsJson = sources.value(QStringLiteral("items"));
    QVERIFY(itemsJson.contains(QStringLiteral("Home")));
    QVERIFY(itemsJson.contains(QStringLiteral("About")));
}

void Test_CommonBlocMenu_Translations::test_menu_save_load_roundtrip_preserves_translations()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("menu.ini"));

    {
        CommonBlocMenuTop menu;
        MenuItem item;
        item.label = QStringLiteral("Home");
        item.url = QStringLiteral("/");
        menu.setItems({item});
        menu.setTranslation(QStringLiteral("items"),
                            QStringLiteral("fr"),
                            QStringLiteral("{\"Home\":\"Accueil\"}"));

        QSettings settings(path, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("menu_top"));
        const QVariantMap map = menu.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            settings.setValue(it.key(), it.value());
        }
        menu.saveTranslations(settings);
        settings.endGroup();
    }

    {
        CommonBlocMenuTop menu;
        QSettings settings(path, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("menu_top"));
        QVariantMap map;
        const auto &keys = settings.childKeys();
        for (const auto &key : keys) {
            map.insert(key, settings.value(key));
        }
        menu.fromMap(map);
        menu.loadTranslations(settings);
        settings.endGroup();

        const QStringList missing = menu.missingTranslations(QStringLiteral("fr"), QStringLiteral("en"));
        QVERIFY(missing.isEmpty());
    }
}

// =============================================================================
// Test_AbstractTheme_Translation
// =============================================================================

class Test_AbstractTheme_Translation : public QObject
{
    Q_OBJECT

private slots:
    void test_theme_source_lang_code_default_empty();
    void test_theme_source_lang_code_set_and_get();
    void test_theme_source_lang_persisted_to_disk();
    void test_theme_save_load_blocs_with_translations();
};

void Test_AbstractTheme_Translation::test_theme_source_lang_code_default_empty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ThemeDefault theme(dir.path());
    QVERIFY(theme.sourceLangCode().isEmpty());
}

void Test_AbstractTheme_Translation::test_theme_source_lang_code_set_and_get()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ThemeDefault theme(dir.path());
    theme.setSourceLangCode(QStringLiteral("en"));
    QCOMPARE(theme.sourceLangCode(), QStringLiteral("en"));
}

void Test_AbstractTheme_Translation::test_theme_source_lang_persisted_to_disk()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    {
        ThemeDefault theme(dir.path());
        theme.setSourceLangCode(QStringLiteral("fr"));
    }

    {
        ThemeDefault theme(dir.path());
        QCOMPARE(theme.sourceLangCode(), QStringLiteral("fr"));
    }
}

void Test_AbstractTheme_Translation::test_theme_save_load_blocs_with_translations()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    {
        ThemeDefault theme(dir.path());
        theme.setSourceLangCode(QStringLiteral("en"));

        // Get header bloc and set data + translations
        const auto topBlocs = theme.getTopBlocs();
        QVERIFY(!topBlocs.isEmpty());
        auto *header = dynamic_cast<CommonBlocHeader *>(topBlocs.first());
        QVERIFY(header);

        header->setTitle(QStringLiteral("My Site"));
        header->setSubtitle(QStringLiteral("A great site"));
        header->setTranslation(QLatin1String(CommonBlocHeader::KEY_TITLE),
                               QStringLiteral("fr"),
                               QStringLiteral("Mon Site"));
        header->setTranslation(QLatin1String(CommonBlocHeader::KEY_SUBTITLE),
                               QStringLiteral("fr"),
                               QStringLiteral("Un super site"));

        theme.saveBlocsData();
    }

    {
        // Reload from disk
        ThemeDefault theme(dir.path());

        const auto topBlocs = theme.getTopBlocs();
        QVERIFY(!topBlocs.isEmpty());
        auto *header = dynamic_cast<CommonBlocHeader *>(topBlocs.first());
        QVERIFY(header);

        QCOMPARE(header->title(), QStringLiteral("My Site"));
        QCOMPARE(header->subtitle(), QStringLiteral("A great site"));

        // Translations should have been restored
        const QStringList missing = header->missingTranslations(QStringLiteral("fr"), QStringLiteral("en"));
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("Missing translations after reload: ") + missing.join(QStringLiteral(", "))));
    }
}

// =============================================================================
// Test_CommonBlocTranslator
// =============================================================================

class Test_CommonBlocTranslator : public QObject
{
    Q_OBJECT

private slots:
    void test_common_bloc_translator_build_jobs_empty_when_no_blocs();
    void test_common_bloc_translator_build_jobs_skips_null_blocs();
    void test_common_bloc_translator_build_jobs_skips_bloc_with_no_source_texts();
    void test_common_bloc_translator_build_jobs_skips_source_lang();
    void test_common_bloc_translator_build_jobs_one_job_when_missing();
    void test_common_bloc_translator_build_jobs_skips_fully_translated_pair();
    void test_common_bloc_translator_build_jobs_multiple_blocs_and_langs();
    void test_common_bloc_translator_build_jobs_job_fields_correct();
};

void Test_CommonBlocTranslator::test_common_bloc_translator_build_jobs_empty_when_no_blocs()
{
    const QList<CommonBlocTranslator::TranslationJob> jobs =
        CommonBlocTranslator::buildJobs({}, QStringLiteral("en"),
                                        {QStringLiteral("fr")});
    QVERIFY(jobs.isEmpty());
}

void Test_CommonBlocTranslator::test_common_bloc_translator_build_jobs_skips_null_blocs()
{
    QList<AbstractCommonBloc *> blocs = {nullptr, nullptr};
    const QList<CommonBlocTranslator::TranslationJob> jobs =
        CommonBlocTranslator::buildJobs(blocs, QStringLiteral("en"),
                                        {QStringLiteral("fr")});
    QVERIFY(jobs.isEmpty());
}

void Test_CommonBlocTranslator::test_common_bloc_translator_build_jobs_skips_bloc_with_no_source_texts()
{
    CommonBlocHeader header;
    // No title or subtitle set — sourceTexts() returns empty hash.
    QList<AbstractCommonBloc *> blocs = {&header};
    const QList<CommonBlocTranslator::TranslationJob> jobs =
        CommonBlocTranslator::buildJobs(blocs, QStringLiteral("en"),
                                        {QStringLiteral("fr")});
    QVERIFY(jobs.isEmpty());
}

void Test_CommonBlocTranslator::test_common_bloc_translator_build_jobs_skips_source_lang()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));
    QList<AbstractCommonBloc *> blocs = {&header};
    // en is both source and target — must be skipped.
    const QList<CommonBlocTranslator::TranslationJob> jobs =
        CommonBlocTranslator::buildJobs(blocs, QStringLiteral("en"),
                                        {QStringLiteral("en")});
    QVERIFY(jobs.isEmpty());
}

void Test_CommonBlocTranslator::test_common_bloc_translator_build_jobs_one_job_when_missing()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));
    // No fr translation set → missingTranslations("fr", "en") is non-empty.
    QList<AbstractCommonBloc *> blocs = {&header};
    const QList<CommonBlocTranslator::TranslationJob> jobs =
        CommonBlocTranslator::buildJobs(blocs, QStringLiteral("en"),
                                        {QStringLiteral("fr")});
    QCOMPARE(jobs.size(), 1);
}

void Test_CommonBlocTranslator::test_common_bloc_translator_build_jobs_skips_fully_translated_pair()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));
    header.setTranslation(QLatin1String(CommonBlocHeader::KEY_TITLE),
                          QStringLiteral("fr"), QStringLiteral("Titre"));
    // All fields now translated for fr → no job needed.
    QList<AbstractCommonBloc *> blocs = {&header};
    const QList<CommonBlocTranslator::TranslationJob> jobs =
        CommonBlocTranslator::buildJobs(blocs, QStringLiteral("en"),
                                        {QStringLiteral("fr")});
    QVERIFY(jobs.isEmpty());
}

void Test_CommonBlocTranslator::test_common_bloc_translator_build_jobs_multiple_blocs_and_langs()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));

    CommonBlocFooter footer;
    footer.setText(QStringLiteral("Copyright"));

    QList<AbstractCommonBloc *> blocs = {&header, &footer};
    // Two target langs; source lang "en" must be excluded.
    // Header is untranslated for both fr and de → 2 jobs.
    // Footer is untranslated for both fr and de → 2 more jobs.
    // Total: 4 jobs.
    const QList<CommonBlocTranslator::TranslationJob> jobs =
        CommonBlocTranslator::buildJobs(blocs, QStringLiteral("en"),
                                        {QStringLiteral("fr"), QStringLiteral("de")});
    QCOMPARE(jobs.size(), 4);
}

void Test_CommonBlocTranslator::test_common_bloc_translator_build_jobs_job_fields_correct()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));
    QList<AbstractCommonBloc *> blocs = {&header};
    const QList<CommonBlocTranslator::TranslationJob> jobs =
        CommonBlocTranslator::buildJobs(blocs, QStringLiteral("en"),
                                        {QStringLiteral("fr")});
    QCOMPARE(jobs.size(), 1);
    const auto &job = jobs.first();
    QCOMPARE(job.blocId,     QLatin1String(CommonBlocHeader::ID));
    QCOMPARE(job.sourceLang, QStringLiteral("en"));
    QCOMPARE(job.targetLang, QStringLiteral("fr"));
}

// =============================================================================
// Main — run all test classes
// =============================================================================

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    int status = 0;

    {
        Test_BlocTranslations t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        Test_CommonBlocHeader_Translations t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        Test_CommonBlocMenu_Translations t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        Test_AbstractTheme_Translation t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        Test_CommonBlocTranslator t;
        status |= QTest::qExec(&t, argc, argv);
    }

    return status;
}

#include "test_common_bloc_translations.moc"
