/**
 * test_website_translations.cpp
 *
 * Maximum-coverage integration tests for the complete translation pipeline:
 *
 *   BlocTranslations (map persistence)
 *   → PageBlocText (translation methods)
 *   → PageBlocSocial (no translation)
 *   → AbstractPageType (routing, aggregation)
 *   → AbstractEngine (available-pages map)
 *   → PageGenerator (generation guard / state machine)
 *   → AbstractCommonBloc (WebCodeAdder bridge)
 *
 * State machine exercised:
 *   "not assessed" (empty langCodesToTranslate) → always generates
 *   "assessed + source lang" → generates without translation check
 *   "assessed + target lang + incomplete" → throws ExceptionWithTitleText
 *   "assessed + target lang + complete" → generates successfully
 *   "assessed + unrelated lang" → page silently skipped
 */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QHash>
#include <QList>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>

#include <atomic>

#include "ExceptionWithTitleText.h"
#include "website/AbstractEngine.h"
#include "website/EngineArticles.h"
#include "website/HostTable.h"
#include "website/WebCodeAdder.h"
#include "website/commonblocs/AbstractCommonBloc.h"
#include "website/commonblocs/BlocTranslations.h"
#include "website/commonblocs/CommonBlocHeader.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageGenerator.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/PermalinkHistoryEntry.h"
#include "website/pages/PageTypeArticle.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/PageBlocText.h"
#include "website/pages/blocs/PageBlocSocial.h"

// =============================================================================
// Shared helpers
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

// Engine with no init() — getLangCode() always returns "" for every index.
EngineArticles g_emptyEngine;

} // namespace

// =============================================================================
// Test_BlocTranslations_MapPersistence
//
// Tests saveToMap() / loadFromMap() — the flat-map persistence used by
// PageBlocText.  QSettings-based persistence is covered by
// test_common_bloc_translations.cpp; this class focuses exclusively on the
// page_data flat-map path.
// =============================================================================

class Test_BlocTranslations_MapPersistence : public QObject
{
    Q_OBJECT

private slots:
    // --- saveToMap ---
    void test_bloctranslations_map_save_stores_text_key();
    void test_bloctranslations_map_save_stores_hash_key();
    void test_bloctranslations_map_save_key_format_tr_lang_field();
    void test_bloctranslations_map_save_hash_key_has_hash_suffix();
    void test_bloctranslations_map_save_empty_translation_not_stored();
    void test_bloctranslations_map_save_multiple_fields_all_stored();
    void test_bloctranslations_map_save_multiple_langs_all_stored();

    // --- loadFromMap ---
    void test_bloctranslations_map_load_restores_translation();
    void test_bloctranslations_map_load_stale_hash_discards();
    void test_bloctranslations_map_load_ignores_non_tr_keys();
    void test_bloctranslations_map_load_ignores_unregistered_field();
    void test_bloctranslations_map_load_hash_only_key_not_an_entry();

    // --- roundtrip ---
    void test_bloctranslations_map_roundtrip_single_field();
    void test_bloctranslations_map_roundtrip_multi_field_multi_lang();
};

void Test_BlocTranslations_MapPersistence::test_bloctranslations_map_save_stores_text_key()
{
    BlocTranslations tr;
    tr.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    tr.setTranslation(QStringLiteral("title"), QStringLiteral("fr"), QStringLiteral("Bonjour"));

    QHash<QString, QString> map;
    tr.saveToMap(map);

    QVERIFY(map.contains(QStringLiteral("tr:fr:title")));
    QCOMPARE(map.value(QStringLiteral("tr:fr:title")), QStringLiteral("Bonjour"));
}

void Test_BlocTranslations_MapPersistence::test_bloctranslations_map_save_stores_hash_key()
{
    BlocTranslations tr;
    tr.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    tr.setTranslation(QStringLiteral("title"), QStringLiteral("fr"), QStringLiteral("Bonjour"));

    QHash<QString, QString> map;
    tr.saveToMap(map);

    QVERIFY(map.contains(QStringLiteral("tr:fr:title:hash")));
    QVERIFY(!map.value(QStringLiteral("tr:fr:title:hash")).isEmpty());
}

void Test_BlocTranslations_MapPersistence::test_bloctranslations_map_save_key_format_tr_lang_field()
{
    BlocTranslations tr;
    tr.setSource(QStringLiteral("subtitle"), QStringLiteral("World"));
    tr.setTranslation(QStringLiteral("subtitle"), QStringLiteral("de"), QStringLiteral("Welt"));

    QHash<QString, QString> map;
    tr.saveToMap(map);

    // Key must be exactly "tr:<lang>:<field>"
    QVERIFY(map.contains(QStringLiteral("tr:de:subtitle")));
    QVERIFY(!map.contains(QStringLiteral("tr:de:")));
}

void Test_BlocTranslations_MapPersistence::test_bloctranslations_map_save_hash_key_has_hash_suffix()
{
    BlocTranslations tr;
    tr.setSource(QStringLiteral("field"), QStringLiteral("text"));
    tr.setTranslation(QStringLiteral("field"), QStringLiteral("es"), QStringLiteral("texto"));

    QHash<QString, QString> map;
    tr.saveToMap(map);

    // Exactly one ":hash" suffix on the hash entry
    const QString hashKey = QStringLiteral("tr:es:field:hash");
    QVERIFY(map.contains(hashKey));
    // The plain text key must not have :hash
    QVERIFY(!map.value(QStringLiteral("tr:es:field")).endsWith(QStringLiteral(":hash")));
}

void Test_BlocTranslations_MapPersistence::test_bloctranslations_map_save_empty_translation_not_stored()
{
    // An entry with empty translated text must not be serialised
    // (the setTranslation in BlocTranslations stores it, but saveToMap should skip it
    // because the empty-text guard in saveToMap filters it out).
    BlocTranslations tr;
    tr.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    tr.setTranslation(QStringLiteral("title"), QStringLiteral("fr"), QString()); // empty!

    QHash<QString, QString> map;
    tr.saveToMap(map);

    // Empty translation must not produce a key
    QVERIFY(!map.contains(QStringLiteral("tr:fr:title")));
}

void Test_BlocTranslations_MapPersistence::test_bloctranslations_map_save_multiple_fields_all_stored()
{
    BlocTranslations tr;
    tr.setSource(QStringLiteral("title"),    QStringLiteral("Hello"));
    tr.setSource(QStringLiteral("subtitle"), QStringLiteral("World"));
    tr.setTranslation(QStringLiteral("title"),    QStringLiteral("fr"), QStringLiteral("Bonjour"));
    tr.setTranslation(QStringLiteral("subtitle"), QStringLiteral("fr"), QStringLiteral("Monde"));

    QHash<QString, QString> map;
    tr.saveToMap(map);

    QVERIFY(map.contains(QStringLiteral("tr:fr:title")));
    QVERIFY(map.contains(QStringLiteral("tr:fr:subtitle")));
    QCOMPARE(map.value(QStringLiteral("tr:fr:title")),    QStringLiteral("Bonjour"));
    QCOMPARE(map.value(QStringLiteral("tr:fr:subtitle")), QStringLiteral("Monde"));
}

void Test_BlocTranslations_MapPersistence::test_bloctranslations_map_save_multiple_langs_all_stored()
{
    BlocTranslations tr;
    tr.setSource(QStringLiteral("text"), QStringLiteral("Hello"));
    tr.setTranslation(QStringLiteral("text"), QStringLiteral("fr"), QStringLiteral("Bonjour"));
    tr.setTranslation(QStringLiteral("text"), QStringLiteral("de"), QStringLiteral("Hallo"));
    tr.setTranslation(QStringLiteral("text"), QStringLiteral("es"), QStringLiteral("Hola"));

    QHash<QString, QString> map;
    tr.saveToMap(map);

    QCOMPARE(map.value(QStringLiteral("tr:fr:text")), QStringLiteral("Bonjour"));
    QCOMPARE(map.value(QStringLiteral("tr:de:text")), QStringLiteral("Hallo"));
    QCOMPARE(map.value(QStringLiteral("tr:es:text")), QStringLiteral("Hola"));
}

void Test_BlocTranslations_MapPersistence::test_bloctranslations_map_load_restores_translation()
{
    // Write then read using a fresh instance
    BlocTranslations writer;
    writer.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    writer.setTranslation(QStringLiteral("title"), QStringLiteral("fr"), QStringLiteral("Bonjour"));
    QHash<QString, QString> map;
    writer.saveToMap(map);

    BlocTranslations reader;
    reader.setSource(QStringLiteral("title"), QStringLiteral("Hello")); // same source
    reader.loadFromMap(map);

    QCOMPARE(reader.translation(QStringLiteral("title"), QStringLiteral("fr")),
             QStringLiteral("Bonjour"));
    QVERIFY(reader.isComplete(QStringLiteral("fr")));
}

void Test_BlocTranslations_MapPersistence::test_bloctranslations_map_load_stale_hash_discards()
{
    // Writer saved with source "Hello"; reader registers different source "Hi"
    BlocTranslations writer;
    writer.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    writer.setTranslation(QStringLiteral("title"), QStringLiteral("fr"), QStringLiteral("Bonjour"));
    QHash<QString, QString> map;
    writer.saveToMap(map);

    BlocTranslations reader;
    reader.setSource(QStringLiteral("title"), QStringLiteral("Hi")); // different → stale
    reader.loadFromMap(map);

    QVERIFY(reader.translation(QStringLiteral("title"), QStringLiteral("fr")).isEmpty());
    QVERIFY(!reader.isComplete(QStringLiteral("fr")));
}

void Test_BlocTranslations_MapPersistence::test_bloctranslations_map_load_ignores_non_tr_keys()
{
    // Non-tr: keys in the map must not be treated as translations
    QHash<QString, QString> map;
    map.insert(QStringLiteral("text"),            QStringLiteral("source text"));
    map.insert(QStringLiteral("0_categories"),    QString());
    map.insert(QStringLiteral("tr:fr:title"),     QStringLiteral("Bonjour"));
    map.insert(QStringLiteral("tr:fr:title:hash"), QStringLiteral("cafebabe"));

    BlocTranslations reader;
    reader.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    // loadFromMap must silently ignore "text" and "0_categories"
    reader.loadFromMap(map); // must not crash

    // Only "tr:fr:title" is a candidate; it won't load because hash is wrong
    QVERIFY(reader.translation(QStringLiteral("title"), QStringLiteral("fr")).isEmpty());
}

void Test_BlocTranslations_MapPersistence::test_bloctranslations_map_load_ignores_unregistered_field()
{
    QHash<QString, QString> map;
    map.insert(QStringLiteral("tr:fr:unknown_field"),       QStringLiteral("some text"));
    map.insert(QStringLiteral("tr:fr:unknown_field:hash"),  QStringLiteral("abc"));

    BlocTranslations reader;
    reader.setSource(QStringLiteral("known"), QStringLiteral("Hello"));
    reader.loadFromMap(map); // must not crash

    // Unknown field → not loaded
    QVERIFY(reader.translation(QStringLiteral("unknown_field"), QStringLiteral("fr")).isEmpty());
}

void Test_BlocTranslations_MapPersistence::test_bloctranslations_map_load_hash_only_key_not_an_entry()
{
    // A ":hash" key alone (no corresponding text key) must not create an entry
    QHash<QString, QString> map;
    map.insert(QStringLiteral("tr:fr:title:hash"), QStringLiteral("somehash"));
    // No "tr:fr:title" text key

    BlocTranslations reader;
    reader.setSource(QStringLiteral("title"), QStringLiteral("Hello"));
    reader.loadFromMap(map); // must not crash

    QVERIFY(reader.translation(QStringLiteral("title"), QStringLiteral("fr")).isEmpty());
}

void Test_BlocTranslations_MapPersistence::test_bloctranslations_map_roundtrip_single_field()
{
    BlocTranslations original;
    original.setSource(QStringLiteral("body"), QStringLiteral("Long text here"));
    original.setTranslation(QStringLiteral("body"), QStringLiteral("it"), QStringLiteral("Testo lungo"));

    QHash<QString, QString> map;
    original.saveToMap(map);

    BlocTranslations restored;
    restored.setSource(QStringLiteral("body"), QStringLiteral("Long text here"));
    restored.loadFromMap(map);

    QCOMPARE(restored.translation(QStringLiteral("body"), QStringLiteral("it")),
             QStringLiteral("Testo lungo"));
    QVERIFY(restored.isComplete(QStringLiteral("it")));
}

void Test_BlocTranslations_MapPersistence::test_bloctranslations_map_roundtrip_multi_field_multi_lang()
{
    BlocTranslations original;
    original.setSource(QStringLiteral("title"),    QStringLiteral("Hello"));
    original.setSource(QStringLiteral("subtitle"), QStringLiteral("World"));
    original.setTranslation(QStringLiteral("title"),    QStringLiteral("fr"), QStringLiteral("Bonjour"));
    original.setTranslation(QStringLiteral("subtitle"), QStringLiteral("fr"), QStringLiteral("Monde"));
    original.setTranslation(QStringLiteral("title"),    QStringLiteral("de"), QStringLiteral("Hallo"));
    original.setTranslation(QStringLiteral("subtitle"), QStringLiteral("de"), QStringLiteral("Welt"));

    QHash<QString, QString> map;
    original.saveToMap(map);

    BlocTranslations restored;
    restored.setSource(QStringLiteral("title"),    QStringLiteral("Hello"));
    restored.setSource(QStringLiteral("subtitle"), QStringLiteral("World"));
    restored.loadFromMap(map);

    QCOMPARE(restored.translation(QStringLiteral("title"),    QStringLiteral("fr")), QStringLiteral("Bonjour"));
    QCOMPARE(restored.translation(QStringLiteral("subtitle"), QStringLiteral("fr")), QStringLiteral("Monde"));
    QCOMPARE(restored.translation(QStringLiteral("title"),    QStringLiteral("de")), QStringLiteral("Hallo"));
    QCOMPARE(restored.translation(QStringLiteral("subtitle"), QStringLiteral("de")), QStringLiteral("Welt"));
    QVERIFY(restored.isComplete(QStringLiteral("fr")));
    QVERIFY(restored.isComplete(QStringLiteral("de")));
}

// =============================================================================
// Test_PageBlocText_Translation
//
// Tests for PageBlocText::collectTranslatables, applyTranslation,
// isTranslationComplete, and translation-aware addCode behaviour.
// =============================================================================

class Test_PageBlocText_Translation : public QObject
{
    Q_OBJECT

private slots:
    // --- collectTranslatables ---
    void test_pagebloctext_collect_nonempty_text_adds_one_field();
    void test_pagebloctext_collect_empty_text_no_fields();
    void test_pagebloctext_collect_field_id_is_key_text();
    void test_pagebloctext_collect_source_text_matches_loaded_text();
    void test_pagebloctext_collect_appends_does_not_clear();

    // --- applyTranslation ---
    void test_pagebloctext_apply_stores_translation();
    void test_pagebloctext_apply_unknown_field_ignored_no_crash();
    void test_pagebloctext_apply_multiple_langs_each_stored();

    // --- isTranslationComplete ---
    void test_pagebloctext_is_complete_false_before_translation();
    void test_pagebloctext_is_complete_true_after_translation();
    void test_pagebloctext_is_complete_true_when_text_empty();
    void test_pagebloctext_is_complete_false_after_source_change();

    // --- save / load roundtrip ---
    void test_pagebloctext_save_load_preserves_translation();
    void test_pagebloctext_save_load_stale_not_reloaded();

    // --- addCode with translation ---
    void test_pagebloctext_addcode_uses_translation_when_available();
    void test_pagebloctext_addcode_falls_back_to_source_no_translation();
};

// Helper: load text, apply translation, run addCode, return html
namespace {
QString htmlFromTextBloc(PageBlocText &bloc,
                          const QString &text,
                          const QString &translationLang = QString(),
                          const QString &translatedText  = QString())
{
    bloc.load({{QLatin1String(PageBlocText::KEY_TEXT), text}});
    if (!translationLang.isEmpty()) {
        bloc.applyTranslation(QStringView{}, QLatin1String(PageBlocText::KEY_TEXT),
                              translationLang, translatedText);
    }
    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    bloc.addCode(QStringView{}, g_emptyEngine, 0, html, css, js, cssDone, jsDone);
    return html;
}
} // namespace

void Test_PageBlocText_Translation::test_pagebloctext_collect_nonempty_text_adds_one_field()
{
    PageBlocText bloc;
    bloc.load({{QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("Some text")}});

    QList<TranslatableField> fields;
    bloc.collectTranslatables(QStringView{}, fields);

    QCOMPARE(fields.size(), 1);
}

void Test_PageBlocText_Translation::test_pagebloctext_collect_empty_text_no_fields()
{
    PageBlocText bloc;
    bloc.load({{QLatin1String(PageBlocText::KEY_TEXT), QString()}});

    QList<TranslatableField> fields;
    bloc.collectTranslatables(QStringView{}, fields);

    QVERIFY(fields.isEmpty());
}

void Test_PageBlocText_Translation::test_pagebloctext_collect_field_id_is_key_text()
{
    PageBlocText bloc;
    bloc.load({{QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("Article body")}});

    QList<TranslatableField> fields;
    bloc.collectTranslatables(QStringView{}, fields);

    QCOMPARE(fields.first().id, QLatin1String(PageBlocText::KEY_TEXT));
}

void Test_PageBlocText_Translation::test_pagebloctext_collect_source_text_matches_loaded_text()
{
    PageBlocText bloc;
    const QString text = QStringLiteral("The original article content");
    bloc.load({{QLatin1String(PageBlocText::KEY_TEXT), text}});

    QList<TranslatableField> fields;
    bloc.collectTranslatables(QStringView{}, fields);

    QCOMPARE(fields.first().sourceText, text);
}

void Test_PageBlocText_Translation::test_pagebloctext_collect_appends_does_not_clear()
{
    // collectTranslatables must APPEND to out, not replace it
    PageBlocText bloc;
    bloc.load({{QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("text")}});

    QList<TranslatableField> fields;
    fields.append({QStringLiteral("pre_existing"), QStringLiteral("already here")});

    bloc.collectTranslatables(QStringView{}, fields);

    QCOMPARE(fields.size(), 2);
    QCOMPARE(fields.at(0).id, QStringLiteral("pre_existing"));
}

void Test_PageBlocText_Translation::test_pagebloctext_apply_stores_translation()
{
    PageBlocText bloc;
    bloc.load({{QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("Hello")}});
    bloc.applyTranslation(QStringView{}, QLatin1String(PageBlocText::KEY_TEXT),
                          QStringLiteral("fr"), QStringLiteral("Bonjour"));

    QVERIFY(bloc.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_PageBlocText_Translation::test_pagebloctext_apply_unknown_field_ignored_no_crash()
{
    PageBlocText bloc;
    bloc.load({{QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("Hello")}});

    // Must not crash
    bloc.applyTranslation(QStringView{}, QStringLiteral("unknown_field"),
                          QStringLiteral("fr"), QStringLiteral("some text"));

    // isComplete must still be false since KEY_TEXT was not translated
    QVERIFY(!bloc.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_PageBlocText_Translation::test_pagebloctext_apply_multiple_langs_each_stored()
{
    PageBlocText bloc;
    bloc.load({{QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("Hello")}});
    bloc.applyTranslation(QStringView{}, QLatin1String(PageBlocText::KEY_TEXT),
                          QStringLiteral("fr"), QStringLiteral("Bonjour"));
    bloc.applyTranslation(QStringView{}, QLatin1String(PageBlocText::KEY_TEXT),
                          QStringLiteral("de"), QStringLiteral("Hallo"));
    bloc.applyTranslation(QStringView{}, QLatin1String(PageBlocText::KEY_TEXT),
                          QStringLiteral("it"), QStringLiteral("Ciao"));

    QVERIFY(bloc.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
    QVERIFY(bloc.isTranslationComplete(QStringView{}, QStringLiteral("de")));
    QVERIFY(bloc.isTranslationComplete(QStringView{}, QStringLiteral("it")));
}

void Test_PageBlocText_Translation::test_pagebloctext_is_complete_false_before_translation()
{
    PageBlocText bloc;
    bloc.load({{QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("Some article text")}});

    QVERIFY(!bloc.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_PageBlocText_Translation::test_pagebloctext_is_complete_true_after_translation()
{
    PageBlocText bloc;
    bloc.load({{QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("Hello")}});
    bloc.applyTranslation(QStringView{}, QLatin1String(PageBlocText::KEY_TEXT),
                          QStringLiteral("fr"), QStringLiteral("Bonjour"));

    QVERIFY(bloc.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_PageBlocText_Translation::test_pagebloctext_is_complete_true_when_text_empty()
{
    // An empty text bloc has no translatable fields → always complete for any lang
    PageBlocText bloc;
    bloc.load({{QLatin1String(PageBlocText::KEY_TEXT), QString()}});

    QVERIFY(bloc.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
    QVERIFY(bloc.isTranslationComplete(QStringView{}, QStringLiteral("de")));
    QVERIFY(bloc.isTranslationComplete(QStringView{}, QStringLiteral("zh")));
}

void Test_PageBlocText_Translation::test_pagebloctext_is_complete_false_after_source_change()
{
    // Translation was set for old source; source is changed → translation purged
    PageBlocText bloc;
    bloc.load({{QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("Hello")}});
    bloc.applyTranslation(QStringView{}, QLatin1String(PageBlocText::KEY_TEXT),
                          QStringLiteral("fr"), QStringLiteral("Bonjour"));
    QVERIFY(bloc.isTranslationComplete(QStringView{}, QStringLiteral("fr")));

    // Reload with different source text → stale translation → incomplete
    bloc.load({{QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("Hello updated")}});
    QVERIFY(!bloc.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_PageBlocText_Translation::test_pagebloctext_save_load_preserves_translation()
{
    PageBlocText writer;
    writer.load({{QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("Original")}});
    writer.applyTranslation(QStringView{}, QLatin1String(PageBlocText::KEY_TEXT),
                            QStringLiteral("de"), QStringLiteral("Original (DE)"));

    QHash<QString, QString> saved;
    writer.save(saved);

    // Load into a fresh bloc and check translation survived
    PageBlocText reader;
    reader.load(saved);

    QVERIFY(reader.isTranslationComplete(QStringView{}, QStringLiteral("de")));
}

void Test_PageBlocText_Translation::test_pagebloctext_save_load_stale_not_reloaded()
{
    PageBlocText writer;
    writer.load({{QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("Original")}});
    writer.applyTranslation(QStringView{}, QLatin1String(PageBlocText::KEY_TEXT),
                            QStringLiteral("fr"), QStringLiteral("Traduction"));

    QHash<QString, QString> saved;
    writer.save(saved);

    // Simulate the source text changing between save and load
    saved.insert(QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("Changed source"));

    PageBlocText reader;
    reader.load(saved);

    // Stale translation must not be loaded
    QVERIFY(!reader.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_PageBlocText_Translation::test_pagebloctext_addcode_uses_translation_when_available()
{
    // The engine with no rows returns getLangCode(0)="" — so we need to use a
    // EngineArticles initialised with a temp dir that has "fr" at index 0.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    {
        QFile csv(QDir(dir.path()).absoluteFilePath(QStringLiteral("engine_domains.csv")));
        QVERIFY(csv.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&csv);
        out << QStringLiteral("Enabled;LangCode;Language;Theme;Domain;HostId;HostFolder\n");
        out << QStringLiteral("1;fr;French;default;fr.example.com;;\n");
    }

    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);
    // getLangCode(0) == "fr"

    PageBlocText bloc;
    bloc.load({{QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("Original text")}});
    bloc.applyTranslation(QStringView{}, QLatin1String(PageBlocText::KEY_TEXT),
                          QStringLiteral("fr"), QStringLiteral("Texte en français"));

    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDone, jsDone);

    QVERIFY(html.contains(QStringLiteral("Texte en français")));
    QVERIFY(!html.contains(QStringLiteral("Original text")));
}

void Test_PageBlocText_Translation::test_pagebloctext_addcode_falls_back_to_source_no_translation()
{
    // No translation set → addCode must emit the source text
    PageBlocText bloc;
    const QString src = QStringLiteral("The source article");
    bloc.load({{QLatin1String(PageBlocText::KEY_TEXT), src}});

    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    bloc.addCode(QStringView{}, g_emptyEngine, 0, html, css, js, cssDone, jsDone);

    QVERIFY(html.contains(src));
}

// =============================================================================
// Test_PageBlocSocial_NoTranslation
//
// PageBlocSocial has no translatable fields; it uses the WebCodeAdder base-class
// no-ops.  These tests confirm that fact explicitly.
// =============================================================================

class Test_PageBlocSocial_NoTranslation : public QObject
{
    Q_OBJECT

private slots:
    void test_pagebloc_social_collect_translatables_empty();
    void test_pagebloc_social_is_complete_always_true_with_content();
    void test_pagebloc_social_apply_translation_no_crash();
    void test_pagebloc_social_is_complete_independent_of_field_content();
};

void Test_PageBlocSocial_NoTranslation::test_pagebloc_social_collect_translatables_empty()
{
    PageBlocSocial bloc;
    bloc.load({
        {QLatin1String(PageBlocSocial::KEY_FB_TITLE), QStringLiteral("Facebook Title")},
        {QLatin1String(PageBlocSocial::KEY_FB_DESC),  QStringLiteral("Facebook Desc")},
        {QLatin1String(PageBlocSocial::KEY_TW_TITLE), QStringLiteral("Twitter Title")},
        {QLatin1String(PageBlocSocial::KEY_TW_DESC),  QStringLiteral("Twitter Desc")},
    });

    QList<TranslatableField> fields;
    bloc.collectTranslatables(QStringView{}, fields);

    QVERIFY(fields.isEmpty());
}

void Test_PageBlocSocial_NoTranslation::test_pagebloc_social_is_complete_always_true_with_content()
{
    PageBlocSocial bloc;
    bloc.load({
        {QLatin1String(PageBlocSocial::KEY_FB_TITLE), QStringLiteral("Some title")},
        {QLatin1String(PageBlocSocial::KEY_LI_DESC),  QStringLiteral("Some description")},
    });

    // No matter what lang is asked, must return true (no override → base class returns true)
    QVERIFY(bloc.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
    QVERIFY(bloc.isTranslationComplete(QStringView{}, QStringLiteral("de")));
    QVERIFY(bloc.isTranslationComplete(QStringView{}, QStringLiteral("zh")));
}

void Test_PageBlocSocial_NoTranslation::test_pagebloc_social_apply_translation_no_crash()
{
    PageBlocSocial bloc;
    bloc.load({{QLatin1String(PageBlocSocial::KEY_FB_TITLE), QStringLiteral("Title")}});

    // Must not crash — silently ignored
    bloc.applyTranslation(QStringView{}, QLatin1String(PageBlocSocial::KEY_FB_TITLE),
                          QStringLiteral("fr"), QStringLiteral("Titre"));
}

void Test_PageBlocSocial_NoTranslation::test_pagebloc_social_is_complete_independent_of_field_content()
{
    // Even with completely filled social fields, no translation is required
    PageBlocSocial bloc;
    bloc.load({
        {QLatin1String(PageBlocSocial::KEY_FB_TITLE), QStringLiteral("FB Title")},
        {QLatin1String(PageBlocSocial::KEY_FB_DESC),  QStringLiteral("FB Desc")},
        {QLatin1String(PageBlocSocial::KEY_TW_TITLE), QStringLiteral("TW Title")},
        {QLatin1String(PageBlocSocial::KEY_TW_DESC),  QStringLiteral("TW Desc")},
        {QLatin1String(PageBlocSocial::KEY_PT_TITLE), QStringLiteral("PT Title")},
        {QLatin1String(PageBlocSocial::KEY_PT_DESC),  QStringLiteral("PT Desc")},
        {QLatin1String(PageBlocSocial::KEY_LI_TITLE), QStringLiteral("LI Title")},
        {QLatin1String(PageBlocSocial::KEY_LI_DESC),  QStringLiteral("LI Desc")},
    });

    QVERIFY(bloc.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

// =============================================================================
// Test_AbstractPageType_TranslationRouting
//
// Tests for AbstractPageType's routing of translation protocol calls across blocs.
// Uses PageTypeArticle as the concrete subclass because:
//   bloc 0 = PageBlocCategory    (no translatable fields)
//   bloc 1 = PageBlocText        (one translatable field: KEY_TEXT)
//   bloc 2 = PageBlocSocial      (no translatable fields)
//   bloc 3 = PageBlocAutoLink    (no translatable fields)
// =============================================================================

class Test_AbstractPageType_TranslationRouting : public QObject
{
    Q_OBJECT

private slots:
    // --- collectTranslatables ---
    void test_pagetype_collect_includes_text_bloc_field_with_prefix();
    void test_pagetype_collect_empty_when_no_text();
    void test_pagetype_collect_social_bloc_contributes_nothing();
    void test_pagetype_collect_prefix_is_bloc_index();

    // --- applyTranslation ---
    void test_pagetype_apply_routes_to_text_bloc_at_index_1();
    void test_pagetype_apply_bad_index_no_crash();
    void test_pagetype_apply_no_separator_no_crash();
    void test_pagetype_apply_nonnumeric_index_no_crash();
    void test_pagetype_apply_out_of_range_index_no_crash();

    // --- isTranslationComplete ---
    void test_pagetype_is_complete_true_for_author_lang();
    void test_pagetype_is_complete_false_no_author_lang_no_translation();
    void test_pagetype_is_complete_false_for_other_lang_before_translation();
    void test_pagetype_is_complete_true_after_translation();
    void test_pagetype_is_complete_true_empty_text_any_lang();
    void test_pagetype_is_complete_true_when_author_lang_not_set_and_text_empty();

    // --- save / load roundtrip preserves translation ---
    void test_pagetype_save_load_roundtrip_preserves_translation();

    // --- addCode uses translation ---
    void test_pagetype_addcode_uses_translated_text();
};

namespace {

struct ArticleTypeFixture {
    QTemporaryDir  dir;
    CategoryTable  cats;
    PageTypeArticle article;

    ArticleTypeFixture()
        : cats(QDir(dir.path()))
        , article(cats)
    {}

    // Convenience: load article with given text, no categories.
    void loadText(const QString &text)
    {
        article.load({
            {QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT), text},
            {QStringLiteral("0_categories"), QString()},
        });
    }
};

} // namespace

void Test_AbstractPageType_TranslationRouting::test_pagetype_collect_includes_text_bloc_field_with_prefix()
{
    ArticleTypeFixture f;
    f.loadText(QStringLiteral("Article body"));

    QList<TranslatableField> fields;
    f.article.collectTranslatables(QStringView{}, fields);

    QCOMPARE(fields.size(), 1);
    // Field id must be "1_text" (bloc index 1, field "text")
    QCOMPARE(fields.first().id,
             QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT));
}

void Test_AbstractPageType_TranslationRouting::test_pagetype_collect_empty_when_no_text()
{
    ArticleTypeFixture f;
    f.loadText(QString()); // empty text

    QList<TranslatableField> fields;
    f.article.collectTranslatables(QStringView{}, fields);

    QVERIFY(fields.isEmpty());
}

void Test_AbstractPageType_TranslationRouting::test_pagetype_collect_social_bloc_contributes_nothing()
{
    // Load social bloc data via the prefixed map; collectTranslatables must not
    // produce entries for bloc 2 (PageBlocSocial has no override)
    ArticleTypeFixture f;
    f.article.load({
        {QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("text")},
        {QStringLiteral("2_") + QLatin1String(PageBlocSocial::KEY_FB_TITLE), QStringLiteral("FB Title")},
        {QStringLiteral("2_") + QLatin1String(PageBlocSocial::KEY_FB_DESC),  QStringLiteral("FB Desc")},
        {QStringLiteral("0_categories"), QString()},
    });

    QList<TranslatableField> fields;
    f.article.collectTranslatables(QStringView{}, fields);

    // Only the text bloc field should appear
    QCOMPARE(fields.size(), 1);
    QVERIFY(fields.first().id.startsWith(QStringLiteral("1_")));
}

void Test_AbstractPageType_TranslationRouting::test_pagetype_collect_prefix_is_bloc_index()
{
    ArticleTypeFixture f;
    f.loadText(QStringLiteral("Some text"));

    QList<TranslatableField> fields;
    f.article.collectTranslatables(QStringView{}, fields);

    QVERIFY(!fields.isEmpty());
    // All field ids must start with "<digit>_"
    for (const TranslatableField &field : std::as_const(fields)) {
        QVERIFY(!field.id.isEmpty());
        QVERIFY(field.id.at(0).isDigit());
        QVERIFY(field.id.contains(QLatin1Char('_')));
    }
}

void Test_AbstractPageType_TranslationRouting::test_pagetype_apply_routes_to_text_bloc_at_index_1()
{
    ArticleTypeFixture f;
    f.loadText(QStringLiteral("Hello"));

    const QString fieldId = QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT);
    f.article.applyTranslation(QStringView{}, fieldId, QStringLiteral("fr"),
                               QStringLiteral("Bonjour"));

    QVERIFY(f.article.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_AbstractPageType_TranslationRouting::test_pagetype_apply_bad_index_no_crash()
{
    ArticleTypeFixture f;
    f.loadText(QStringLiteral("Hello"));

    // "abc_text" — non-numeric index — must be silently ignored
    f.article.applyTranslation(QStringView{}, QStringLiteral("abc_text"),
                               QStringLiteral("fr"), QStringLiteral("text"));
    // Must not have crashed; isTranslationComplete must still be false
    QVERIFY(!f.article.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_AbstractPageType_TranslationRouting::test_pagetype_apply_no_separator_no_crash()
{
    ArticleTypeFixture f;
    f.loadText(QStringLiteral("Hello"));

    // No underscore separator — must be silently ignored
    f.article.applyTranslation(QStringView{}, QStringLiteral("text"),
                               QStringLiteral("fr"), QStringLiteral("text"));
    QVERIFY(!f.article.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_AbstractPageType_TranslationRouting::test_pagetype_apply_nonnumeric_index_no_crash()
{
    ArticleTypeFixture f;
    f.loadText(QStringLiteral("Hello"));

    f.article.applyTranslation(QStringView{}, QStringLiteral("x_text"),
                               QStringLiteral("fr"), QStringLiteral("text"));
    QVERIFY(!f.article.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_AbstractPageType_TranslationRouting::test_pagetype_apply_out_of_range_index_no_crash()
{
    ArticleTypeFixture f;
    f.loadText(QStringLiteral("Hello"));

    // Index 99 is out of range for 4-bloc article
    f.article.applyTranslation(QStringView{}, QStringLiteral("99_text"),
                               QStringLiteral("fr"), QStringLiteral("text"));
    QVERIFY(!f.article.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_AbstractPageType_TranslationRouting::test_pagetype_is_complete_true_for_author_lang()
{
    ArticleTypeFixture f;
    f.loadText(QStringLiteral("English article content"));
    f.article.setAuthorLang(QStringLiteral("en"));

    // Source language always complete — no translation needed
    QVERIFY(f.article.isTranslationComplete(QStringView{}, QStringLiteral("en")));
}

void Test_AbstractPageType_TranslationRouting::test_pagetype_is_complete_false_no_author_lang_no_translation()
{
    // When author lang is not set (empty) and no translation provided,
    // isTranslationComplete may return false or true depending on blocs.
    // For a non-empty text bloc with no translation set: false.
    ArticleTypeFixture f;
    f.loadText(QStringLiteral("Some text"));
    // Do NOT call setAuthorLang

    // With no authorLang and no translation, text bloc returns false → aggregate false
    QVERIFY(!f.article.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_AbstractPageType_TranslationRouting::test_pagetype_is_complete_false_for_other_lang_before_translation()
{
    ArticleTypeFixture f;
    f.loadText(QStringLiteral("Article text"));
    f.article.setAuthorLang(QStringLiteral("en"));

    QVERIFY(!f.article.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_AbstractPageType_TranslationRouting::test_pagetype_is_complete_true_after_translation()
{
    ArticleTypeFixture f;
    f.loadText(QStringLiteral("Hello world"));
    f.article.setAuthorLang(QStringLiteral("en"));

    const QString fieldId = QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT);
    f.article.applyTranslation(QStringView{}, fieldId, QStringLiteral("fr"),
                               QStringLiteral("Bonjour monde"));

    QVERIFY(f.article.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_AbstractPageType_TranslationRouting::test_pagetype_is_complete_true_empty_text_any_lang()
{
    ArticleTypeFixture f;
    f.loadText(QString()); // empty → nothing to translate
    f.article.setAuthorLang(QStringLiteral("en"));

    QVERIFY(f.article.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
    QVERIFY(f.article.isTranslationComplete(QStringView{}, QStringLiteral("de")));
    QVERIFY(f.article.isTranslationComplete(QStringView{}, QStringLiteral("zh")));
}

void Test_AbstractPageType_TranslationRouting::test_pagetype_is_complete_true_when_author_lang_not_set_and_text_empty()
{
    ArticleTypeFixture f;
    f.loadText(QString());
    // No authorLang set, no text → complete for any lang
    QVERIFY(f.article.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_AbstractPageType_TranslationRouting::test_pagetype_save_load_roundtrip_preserves_translation()
{
    ArticleTypeFixture writer;
    writer.loadText(QStringLiteral("The article text"));
    writer.article.setAuthorLang(QStringLiteral("en"));

    const QString fieldId = QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT);
    writer.article.applyTranslation(QStringView{}, fieldId, QStringLiteral("de"),
                                    QStringLiteral("Der Artikeltext"));

    QHash<QString, QString> saved;
    writer.article.save(saved);

    ArticleTypeFixture reader;
    reader.article.setAuthorLang(QStringLiteral("en"));
    reader.article.load(saved);

    QVERIFY(reader.article.isTranslationComplete(QStringView{}, QStringLiteral("de")));
}

void Test_AbstractPageType_TranslationRouting::test_pagetype_addcode_uses_translated_text()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Write CSV with "fr" at index 0
    {
        QFile csv(QDir(dir.path()).absoluteFilePath(QStringLiteral("engine_domains.csv")));
        QVERIFY(csv.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&csv);
        out << QStringLiteral("Enabled;LangCode;Language;Theme;Domain;HostId;HostFolder\n");
        out << QStringLiteral("1;fr;French;default;fr.example.com;;\n");
    }
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    CategoryTable cats(QDir(dir.path()));
    PageTypeArticle article(cats);
    article.setAuthorLang(QStringLiteral("en"));
    article.load({
        {QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("English text")},
        {QStringLiteral("0_categories"), QString()},
    });
    const QString fieldId = QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT);
    article.applyTranslation(QStringView{}, fieldId, QStringLiteral("fr"),
                             QStringLiteral("Texte français"));

    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    article.addCode(QStringView{}, engine, 0, html, css, js, cssDone, jsDone);

    QVERIFY(html.contains(QStringLiteral("Texte français")));
    QVERIFY(!html.contains(QStringLiteral("English text")));
}

// =============================================================================
// Test_AbstractEngine_AvailablePages
//
// Tests for AbstractEngine::setAvailablePages / isPageAvailable.
// =============================================================================

class Test_AbstractEngine_AvailablePages : public QObject
{
    Q_OBJECT

private slots:
    void test_engine_permissive_when_map_not_set();
    void test_engine_returns_true_for_known_page_and_lang();
    void test_engine_returns_false_for_unknown_page();
    void test_engine_returns_false_for_unknown_lang();
    void test_engine_set_replaces_previous_map();
    void test_engine_set_empty_reverts_to_permissive();
    void test_engine_multiple_langs_independent();
    void test_engine_multiple_pages_in_same_lang();
};

void Test_AbstractEngine_AvailablePages::test_engine_permissive_when_map_not_set()
{
    EngineArticles engine;
    // No setAvailablePages call — permissive default
    QVERIFY(engine.isPageAvailable(QStringLiteral("/any/page.html"), 0));
    QVERIFY(engine.isPageAvailable(QStringLiteral("/another.html"),  0));
}

void Test_AbstractEngine_AvailablePages::test_engine_returns_true_for_known_page_and_lang()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    {
        QFile csv(QDir(dir.path()).absoluteFilePath(QStringLiteral("engine_domains.csv")));
        QVERIFY(csv.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&csv);
        out << QStringLiteral("Enabled;LangCode;Language;Theme;Domain;HostId;HostFolder\n");
        out << QStringLiteral("1;fr;French;default;fr.example.com;;\n");
    }
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    QHash<QString, QSet<QString>> map;
    map[QStringLiteral("fr")].insert(QStringLiteral("/article.html"));
    engine.setAvailablePages(map);

    QVERIFY(engine.isPageAvailable(QStringLiteral("/article.html"), 0));
}

void Test_AbstractEngine_AvailablePages::test_engine_returns_false_for_unknown_page()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    {
        QFile csv(QDir(dir.path()).absoluteFilePath(QStringLiteral("engine_domains.csv")));
        QVERIFY(csv.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&csv);
        out << QStringLiteral("Enabled;LangCode;Language;Theme;Domain;HostId;HostFolder\n");
        out << QStringLiteral("1;fr;French;default;fr.example.com;;\n");
    }
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    QHash<QString, QSet<QString>> map;
    map[QStringLiteral("fr")].insert(QStringLiteral("/known.html"));
    engine.setAvailablePages(map);

    QVERIFY(!engine.isPageAvailable(QStringLiteral("/unknown.html"), 0));
}

void Test_AbstractEngine_AvailablePages::test_engine_returns_false_for_unknown_lang()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    {
        QFile csv(QDir(dir.path()).absoluteFilePath(QStringLiteral("engine_domains.csv")));
        QVERIFY(csv.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&csv);
        out << QStringLiteral("Enabled;LangCode;Language;Theme;Domain;HostId;HostFolder\n");
        out << QStringLiteral("1;de;German;default;de.example.com;;\n"); // index 0 = de
    }
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    QHash<QString, QSet<QString>> map;
    // Map has only "fr" pages; engine at index 0 returns "de"
    map[QStringLiteral("fr")].insert(QStringLiteral("/article.html"));
    engine.setAvailablePages(map);

    QVERIFY(!engine.isPageAvailable(QStringLiteral("/article.html"), 0));
}

void Test_AbstractEngine_AvailablePages::test_engine_set_replaces_previous_map()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    {
        QFile csv(QDir(dir.path()).absoluteFilePath(QStringLiteral("engine_domains.csv")));
        QVERIFY(csv.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&csv);
        out << QStringLiteral("Enabled;LangCode;Language;Theme;Domain;HostId;HostFolder\n");
        out << QStringLiteral("1;fr;French;default;fr.example.com;;\n");
    }
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    QHash<QString, QSet<QString>> map1;
    map1[QStringLiteral("fr")].insert(QStringLiteral("/page-a.html"));
    engine.setAvailablePages(map1);
    QVERIFY(engine.isPageAvailable(QStringLiteral("/page-a.html"), 0));

    QHash<QString, QSet<QString>> map2;
    map2[QStringLiteral("fr")].insert(QStringLiteral("/page-b.html"));
    engine.setAvailablePages(map2);

    // Old page no longer available
    QVERIFY(!engine.isPageAvailable(QStringLiteral("/page-a.html"), 0));
    // New page available
    QVERIFY(engine.isPageAvailable(QStringLiteral("/page-b.html"), 0));
}

void Test_AbstractEngine_AvailablePages::test_engine_set_empty_reverts_to_permissive()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    {
        QFile csv(QDir(dir.path()).absoluteFilePath(QStringLiteral("engine_domains.csv")));
        QVERIFY(csv.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&csv);
        out << QStringLiteral("Enabled;LangCode;Language;Theme;Domain;HostId;HostFolder\n");
        out << QStringLiteral("1;fr;French;default;fr.example.com;;\n");
    }
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    QHash<QString, QSet<QString>> restrictive;
    restrictive[QStringLiteral("fr")].insert(QStringLiteral("/only-this.html"));
    engine.setAvailablePages(restrictive);
    QVERIFY(!engine.isPageAvailable(QStringLiteral("/other.html"), 0));

    // Setting empty map reverts to permissive
    engine.setAvailablePages({});
    QVERIFY(engine.isPageAvailable(QStringLiteral("/other.html"), 0));
}

void Test_AbstractEngine_AvailablePages::test_engine_multiple_langs_independent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    {
        QFile csv(QDir(dir.path()).absoluteFilePath(QStringLiteral("engine_domains.csv")));
        QVERIFY(csv.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&csv);
        out << QStringLiteral("Enabled;LangCode;Language;Theme;Domain;HostId;HostFolder\n");
        out << QStringLiteral("1;fr;French;default;fr.example.com;;\n");
        out << QStringLiteral("1;de;German;default;de.example.com;;\n");
    }
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    QHash<QString, QSet<QString>> map;
    map[QStringLiteral("fr")].insert(QStringLiteral("/article.html"));
    // "de" has NO pages
    engine.setAvailablePages(map);

    QVERIFY(engine.isPageAvailable(QStringLiteral("/article.html"), 0));  // fr → true
    QVERIFY(!engine.isPageAvailable(QStringLiteral("/article.html"), 1)); // de → false
}

void Test_AbstractEngine_AvailablePages::test_engine_multiple_pages_in_same_lang()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    {
        QFile csv(QDir(dir.path()).absoluteFilePath(QStringLiteral("engine_domains.csv")));
        QVERIFY(csv.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&csv);
        out << QStringLiteral("Enabled;LangCode;Language;Theme;Domain;HostId;HostFolder\n");
        out << QStringLiteral("1;fr;French;default;fr.example.com;;\n");
    }
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    QHash<QString, QSet<QString>> map;
    map[QStringLiteral("fr")].insert(QStringLiteral("/page1.html"));
    map[QStringLiteral("fr")].insert(QStringLiteral("/page2.html"));
    map[QStringLiteral("fr")].insert(QStringLiteral("/page3.html"));
    engine.setAvailablePages(map);

    QVERIFY(engine.isPageAvailable(QStringLiteral("/page1.html"), 0));
    QVERIFY(engine.isPageAvailable(QStringLiteral("/page2.html"), 0));
    QVERIFY(engine.isPageAvailable(QStringLiteral("/page3.html"), 0));
    QVERIFY(!engine.isPageAvailable(QStringLiteral("/page4.html"), 0));
}

// =============================================================================
// Test_PageGenerator_TranslationGuard
//
// End-to-end tests for the translation state machine in PageGenerator::generateAll.
//
// State machine:
//   langCodesToTranslate = [] (unassessed) → generates always (backward compat)
//   lang == currentLang               → source page → generates
//   lang != currentLang, currentLang in targets, incomplete  → throws
//   lang != currentLang, currentLang in targets, complete    → generates
//   currentLang not source and not in targets                → skipped
//
// Engine setup: engine CSV written before init() so indices map to known langs.
//   index 0 → "fr"   (source language of all test pages)
//   index 1 → "de"   (target language; pages translated to German)
//   index 2 → "it"   (unrelated language; no pages target Italian)
// =============================================================================

namespace {

struct TranslationGenFixture {
    QTemporaryDir    dir;
    CategoryTable    categoryTable;
    PageDb           db;
    PageRepositoryDb repo;
    PageGenerator    gen;
    EngineArticles   engine;
    HostTable        hostTable;

    TranslationGenFixture()
        : categoryTable(QDir(dir.path()))
        , db(QDir(dir.path()))
        , repo(db)
        , gen(repo, categoryTable)
        , hostTable(QDir(dir.path()))
    {
        // Write engine CSV before init() so indices map to known langs:
        //   index 0 = fr, index 1 = de, index 2 = it
        writeEngineCsv();
        engine.init(QDir(dir.path()), hostTable);
    }

    void writeEngineCsv()
    {
        QFile csv(QDir(dir.path()).absoluteFilePath(QStringLiteral("engine_domains.csv")));
        if (!csv.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return;
        }
        QTextStream out(&csv);
        out << QStringLiteral("Enabled;LangCode;Language;Theme;Domain;HostId;HostFolder\n");
        out << QStringLiteral("1;fr;French;default;fr.example.com;;\n");
        out << QStringLiteral("1;de;German;default;de.example.com;;\n");
        out << QStringLiteral("1;it;Italian;default;it.example.com;;\n");
    }

    // Creates a source article page (lang="fr", no targets assessed yet).
    int addUnassessedArticle(const QString &permalink, const QString &text)
    {
        const int id = repo.create(QStringLiteral("article"), permalink,
                                   QStringLiteral("fr"));
        repo.saveData(id, {
            {QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT), text},
            {QStringLiteral("0_categories"), QString()},
        });
        return id;
    }

    // Creates an assessed page: lang="fr", targets=["de"].
    int addAssessedArticle(const QString &permalink, const QString &text)
    {
        const int id = addUnassessedArticle(permalink, text);
        repo.setLangCodesToTranslate(id, {QStringLiteral("de")});
        return id;
    }

    // Adds a German translation to the page data (in-map translation stored inline).
    void addGermanTranslation(int id, const QString &translatedText)
    {
        QHash<QString, QString> data = repo.loadData(id);

        const QString srcText = data.value(
            QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT));

        // Simulate what PageTranslator does: load page type, apply, save
        auto type = AbstractPageType::createForTypeId(QStringLiteral("article"),
                                                      categoryTable);
        type->load(data);
        type->setAuthorLang(QStringLiteral("fr"));

        const QString fieldId = QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT);
        type->applyTranslation(QStringView{}, fieldId, QStringLiteral("de"), translatedText);

        type->save(data); // saves tr:de:text + tr:de:text:hash into data
        repo.saveData(id, data);
    }

    // Opens content.db and returns a unique connection name.
    QString openContentDb()
    {
        static std::atomic<int> s_counter{0};
        const QString conn = QStringLiteral("tr_gen_test_db_")
                             + QString::number(s_counter.fetch_add(1));
        QSqlDatabase db2 = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db2.setDatabaseName(QDir(dir.path()).filePath(
            QLatin1StringView(PageGenerator::FILENAME)));
        db2.open();
        return conn;
    }

    void closeContentDb(const QString &conn)
    {
        { QSqlDatabase::database(conn).close(); }
        QSqlDatabase::removeDatabase(conn);
    }
};

} // namespace

class Test_PageGenerator_TranslationGuard : public QObject
{
    Q_OBJECT

private slots:
    // State machine: unassessed (backward compat)
    void test_generator_unassessed_page_generates_for_any_lang();
    void test_generator_unassessed_page_count_equals_page_count();

    // State machine: source language
    void test_generator_source_lang_generates_without_guard();

    // State machine: target language
    void test_generator_target_lang_incomplete_translation_raises();
    void test_generator_target_lang_complete_translation_generates();
    void test_generator_target_lang_empty_text_generates_without_translation();

    // State machine: unrelated language
    void test_generator_unrelated_lang_skips_page();
    void test_generator_unrelated_lang_returns_zero_count();

    // Available pages map
    void test_generator_available_pages_set_for_source_lang();
    void test_generator_available_pages_set_for_target_lang();
    void test_generator_unassessed_page_in_available_pages_for_current_lang();
};

void Test_PageGenerator_TranslationGuard::test_generator_unassessed_page_generates_for_any_lang()
{
    // Unassessed page (langCodesToTranslate = []) must generate regardless of engine lang.
    // Here engine index 1 → "de", but the page has lang="fr" and no targets.
    TranslationGenFixture f;
    f.addUnassessedArticle(QStringLiteral("/unassessed.html"), QStringLiteral("text"));

    // index 1 → "de"
    const int count = f.gen.generateAll(QDir(f.dir.path()),
                                        QStringLiteral("de.example.com"), f.engine, 1);
    QCOMPARE(count, 1);
}

void Test_PageGenerator_TranslationGuard::test_generator_unassessed_page_count_equals_page_count()
{
    TranslationGenFixture f;
    f.addUnassessedArticle(QStringLiteral("/p1.html"), QStringLiteral("text1"));
    f.addUnassessedArticle(QStringLiteral("/p2.html"), QStringLiteral("text2"));
    f.addUnassessedArticle(QStringLiteral("/p3.html"), QStringLiteral("text3"));

    // All three pages unassessed → generate all three
    const int count = f.gen.generateAll(QDir(f.dir.path()),
                                        QStringLiteral("de.example.com"), f.engine, 1);
    QCOMPARE(count, 3);
}

void Test_PageGenerator_TranslationGuard::test_generator_source_lang_generates_without_guard()
{
    // Page lang="fr", target="de". Engine index 0 → "fr".
    // isSourceLang=true → generates without translation check.
    TranslationGenFixture f;
    f.addAssessedArticle(QStringLiteral("/article.html"), QStringLiteral("French content"));

    // index 0 → "fr" (source lang = author lang)
    const int count = f.gen.generateAll(QDir(f.dir.path()),
                                        QStringLiteral("fr.example.com"), f.engine, 0);
    QCOMPARE(count, 1);
}

void Test_PageGenerator_TranslationGuard::test_generator_target_lang_incomplete_translation_raises()
{
    // Page lang="fr", target="de". Engine index 1 → "de". No translation stored.
    TranslationGenFixture f;
    f.addAssessedArticle(QStringLiteral("/article.html"), QStringLiteral("French content"));

    QVERIFY(throwsException([&] {
        f.gen.generateAll(QDir(f.dir.path()),
                          QStringLiteral("de.example.com"), f.engine, 1);
    }));
}

void Test_PageGenerator_TranslationGuard::test_generator_target_lang_complete_translation_generates()
{
    // Page lang="fr", target="de". German translation stored. Engine index 1 → "de".
    TranslationGenFixture f;
    const int id = f.addAssessedArticle(QStringLiteral("/article.html"),
                                        QStringLiteral("French content"));
    f.addGermanTranslation(id, QStringLiteral("Deutschsprachiger Inhalt"));

    const int count = f.gen.generateAll(QDir(f.dir.path()),
                                        QStringLiteral("de.example.com"), f.engine, 1);
    QCOMPARE(count, 1);
}

void Test_PageGenerator_TranslationGuard::test_generator_target_lang_empty_text_generates_without_translation()
{
    // A page with empty text has nothing to translate → always complete → no exception.
    TranslationGenFixture f;
    f.addAssessedArticle(QStringLiteral("/empty.html"), QString() /* empty text */);

    // No translation needed for empty text
    const int count = f.gen.generateAll(QDir(f.dir.path()),
                                        QStringLiteral("de.example.com"), f.engine, 1);
    QCOMPARE(count, 1);
}

void Test_PageGenerator_TranslationGuard::test_generator_unrelated_lang_skips_page()
{
    // Page lang="fr", target=["de"]. Engine index 2 → "it".
    // "it" is neither source nor target → page skipped.
    TranslationGenFixture f;
    f.addAssessedArticle(QStringLiteral("/article.html"), QStringLiteral("content"));

    // No exception — page is simply skipped
    QVERIFY(!throwsException([&] {
        f.gen.generateAll(QDir(f.dir.path()),
                          QStringLiteral("it.example.com"), f.engine, 2);
    }));
}

void Test_PageGenerator_TranslationGuard::test_generator_unrelated_lang_returns_zero_count()
{
    TranslationGenFixture f;
    f.addAssessedArticle(QStringLiteral("/article.html"), QStringLiteral("content"));

    const int count = f.gen.generateAll(QDir(f.dir.path()),
                                        QStringLiteral("it.example.com"), f.engine, 2);
    QCOMPARE(count, 0);
}

void Test_PageGenerator_TranslationGuard::test_generator_available_pages_set_for_source_lang()
{
    // After generateAll for source lang ("fr"), the engine's available-pages map
    // must include the permalink under "fr".
    TranslationGenFixture f;
    f.addAssessedArticle(QStringLiteral("/source-page.html"), QStringLiteral("content"));

    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("fr.example.com"), f.engine, 0);

    QVERIFY(f.engine.isPageAvailable(QStringLiteral("/source-page.html"), 0)); // 0 → "fr"
}

void Test_PageGenerator_TranslationGuard::test_generator_available_pages_set_for_target_lang()
{
    // After generateAll for target lang ("de"), the engine's available-pages map
    // must include the permalink under "de".
    TranslationGenFixture f;
    const int id = f.addAssessedArticle(QStringLiteral("/target-page.html"),
                                        QStringLiteral("content"));
    f.addGermanTranslation(id, QStringLiteral("Inhalt"));

    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("de.example.com"), f.engine, 1);

    QVERIFY(f.engine.isPageAvailable(QStringLiteral("/target-page.html"), 1)); // 1 → "de"
}

void Test_PageGenerator_TranslationGuard::test_generator_unassessed_page_in_available_pages_for_current_lang()
{
    // An unassessed page (no targets) is indexed under currentLang in the
    // available-pages map (backward-compat: it's considered available in the
    // language currently being generated).
    TranslationGenFixture f;
    f.addUnassessedArticle(QStringLiteral("/unassessed.html"), QStringLiteral("text"));

    // Generate for "de" (index 1)
    f.gen.generateAll(QDir(f.dir.path()), QStringLiteral("de.example.com"), f.engine, 1);

    // The page should be registered as available for "de" (current lang), not "fr" (page lang)
    QVERIFY(f.engine.isPageAvailable(QStringLiteral("/unassessed.html"), 1)); // 1 → "de"
}

// =============================================================================
// Test_AbstractCommonBloc_WebCodeAdderBridge
//
// Tests that AbstractCommonBloc's override of the three WebCodeAdder translation
// methods correctly delegates to sourceTexts() / setTranslation() / always-true.
// Uses CommonBlocHeader as the concrete implementation.
// =============================================================================

class Test_AbstractCommonBloc_WebCodeAdderBridge : public QObject
{
    Q_OBJECT

private slots:
    void test_commonbloc_collect_delegates_to_source_texts();
    void test_commonbloc_collect_excludes_empty_source_fields();
    void test_commonbloc_collect_empty_when_no_source_texts();
    void test_commonbloc_apply_delegates_to_set_translation();
    void test_commonbloc_is_complete_always_true();
    void test_commonbloc_is_complete_true_even_with_missing_translations();
    void test_commonbloc_collect_appends_does_not_clear();
};

void Test_AbstractCommonBloc_WebCodeAdderBridge::test_commonbloc_collect_delegates_to_source_texts()
{
    // CommonBlocHeader::sourceTexts() returns {KEY_TITLE: title, KEY_SUBTITLE: subtitle}
    // when both are non-empty.  collectTranslatables must mirror this.
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Site Title"));
    header.setSubtitle(QStringLiteral("Tagline"));

    QList<TranslatableField> fields;
    header.collectTranslatables(QStringView{}, fields);

    QCOMPARE(fields.size(), 2);
    bool hasTitle    = false;
    bool hasSubtitle = false;
    for (const TranslatableField &f : std::as_const(fields)) {
        if (f.id == QLatin1String(CommonBlocHeader::KEY_TITLE)) {
            hasTitle = true;
            QCOMPARE(f.sourceText, QStringLiteral("Site Title"));
        }
        if (f.id == QLatin1String(CommonBlocHeader::KEY_SUBTITLE)) {
            hasSubtitle = true;
            QCOMPARE(f.sourceText, QStringLiteral("Tagline"));
        }
    }
    QVERIFY(hasTitle);
    QVERIFY(hasSubtitle);
}

void Test_AbstractCommonBloc_WebCodeAdderBridge::test_commonbloc_collect_excludes_empty_source_fields()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));
    // subtitle is empty by default

    QList<TranslatableField> fields;
    header.collectTranslatables(QStringView{}, fields);

    QCOMPARE(fields.size(), 1);
    QCOMPARE(fields.first().id, QLatin1String(CommonBlocHeader::KEY_TITLE));
}

void Test_AbstractCommonBloc_WebCodeAdderBridge::test_commonbloc_collect_empty_when_no_source_texts()
{
    // A freshly constructed header with no setTitle / setSubtitle has no source texts.
    CommonBlocHeader header;

    QList<TranslatableField> fields;
    header.collectTranslatables(QStringView{}, fields);

    QVERIFY(fields.isEmpty());
}

void Test_AbstractCommonBloc_WebCodeAdderBridge::test_commonbloc_apply_delegates_to_set_translation()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));

    // Call applyTranslation (WebCodeAdder interface)
    header.applyTranslation(QStringView{}, QLatin1String(CommonBlocHeader::KEY_TITLE),
                            QStringLiteral("fr"), QStringLiteral("Mon Site"));

    // Effect must be visible via missingTranslations (which goes through setTranslation internally)
    const QStringList missing = header.missingTranslations(QStringLiteral("fr"),
                                                           QStringLiteral("en"));
    QVERIFY(missing.isEmpty());
}

void Test_AbstractCommonBloc_WebCodeAdderBridge::test_commonbloc_is_complete_always_true()
{
    // AbstractCommonBloc::isTranslationComplete always returns true.
    // Common blocs use assertTranslated() for their own guard; they must not
    // block the page-level generation guard in PageGenerator.
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));
    // NO translation set for "fr"

    QVERIFY(header.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_AbstractCommonBloc_WebCodeAdderBridge::test_commonbloc_is_complete_true_even_with_missing_translations()
{
    // Even when assertTranslated() would raise, isTranslationComplete returns true.
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));
    header.setSubtitle(QStringLiteral("Subtitle"));
    // Neither translated

    // isTranslationComplete must return true regardless
    QVERIFY(header.isTranslationComplete(QStringView{}, QStringLiteral("de")));

    // assertTranslated would raise (confirming translations really are missing)
    QVERIFY(throwsException([&] {
        header.assertTranslated(QStringLiteral("de"), QStringLiteral("en"));
    }));
}

void Test_AbstractCommonBloc_WebCodeAdderBridge::test_commonbloc_collect_appends_does_not_clear()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("Title"));

    QList<TranslatableField> fields;
    fields.append({QStringLiteral("pre_existing"), QStringLiteral("already here")});

    header.collectTranslatables(QStringView{}, fields);

    // Must have the pre-existing field AND the header title field
    QCOMPARE(fields.size(), 2);
    QCOMPARE(fields.at(0).id, QStringLiteral("pre_existing"));
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    int status = 0;

    {
        Test_BlocTranslations_MapPersistence t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        Test_PageBlocText_Translation t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        Test_PageBlocSocial_NoTranslation t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        Test_AbstractPageType_TranslationRouting t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        Test_AbstractEngine_AvailablePages t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        Test_PageGenerator_TranslationGuard t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        Test_AbstractCommonBloc_WebCodeAdderBridge t;
        status |= QTest::qExec(&t, argc, argv);
    }

    return status;
}

#include "test_website_translations.moc"
