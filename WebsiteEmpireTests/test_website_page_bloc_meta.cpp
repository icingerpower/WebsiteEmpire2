#include <QtTest>

#include "website/pages/blocs/PageBlocMeta.h"
#include "website/pages/blocs/widgets/AbstractPageBlockWidget.h"
#include "website/EngineArticles.h"

// =============================================================================
// Helpers
// =============================================================================

namespace {

EngineArticles engine;

QHash<QString, QString> makeHash(const QString &title, const QString &desc)
{
    return {
        {QLatin1String(PageBlocMeta::KEY_SEO_TITLE), title},
        {QLatin1String(PageBlocMeta::KEY_SEO_DESC),  desc},
    };
}

} // namespace

// =============================================================================
// Test_Website_PageBlocMeta
// =============================================================================

class Test_Website_PageBlocMeta : public QObject
{
    Q_OBJECT

private slots:
    // --- getName ---
    void test_meta_get_name_is_not_empty();

    // --- getAiKeyClues ---
    void test_meta_ai_key_clues_contains_seo_title_key();
    void test_meta_ai_key_clues_contains_seo_desc_key();
    void test_meta_ai_key_clues_values_non_empty();

    // --- createEditWidget ---
    void test_meta_create_edit_widget_returns_non_null();

    // --- addCode: no-op ---
    void test_meta_addcode_emits_no_html();
    void test_meta_addcode_emits_no_css();
    void test_meta_addcode_emits_no_js();

    // --- load / save ---
    void test_meta_load_save_roundtrip_title();
    void test_meta_load_save_roundtrip_desc();
    void test_meta_load_empty_hash_produces_empty_values();
    void test_meta_save_contains_title_key();
    void test_meta_save_contains_desc_key();

    // --- accessors: source fallback ---
    void test_meta_seo_title_returns_source_when_no_translation();
    void test_meta_seo_desc_returns_source_when_no_translation();
    void test_meta_seo_title_returns_translation_when_available();
    void test_meta_seo_desc_returns_translation_when_available();
    void test_meta_seo_title_empty_when_not_loaded();
    void test_meta_seo_desc_empty_when_not_loaded();

    // --- translation protocol ---
    void test_meta_collect_translatables_empty_when_no_content();
    void test_meta_collect_translatables_includes_title_when_set();
    void test_meta_collect_translatables_includes_desc_when_set();
    void test_meta_collect_translatables_two_fields_both_non_empty();
    void test_meta_apply_translation_updates_seo_title();
    void test_meta_apply_translation_updates_seo_desc();
    void test_meta_is_translation_complete_false_before_any();
    void test_meta_is_translation_complete_true_after_both();
    void test_meta_is_translation_complete_false_after_only_title();
};

// =============================================================================
// getName
// =============================================================================

void Test_Website_PageBlocMeta::test_meta_get_name_is_not_empty()
{
    PageBlocMeta bloc;
    QVERIFY(!bloc.getName().isEmpty());
}

// =============================================================================
// getAiKeyClues
// =============================================================================

void Test_Website_PageBlocMeta::test_meta_ai_key_clues_contains_seo_title_key()
{
    PageBlocMeta bloc;
    QVERIFY(bloc.getAiKeyClues().contains(QLatin1String(PageBlocMeta::KEY_SEO_TITLE)));
}

void Test_Website_PageBlocMeta::test_meta_ai_key_clues_contains_seo_desc_key()
{
    PageBlocMeta bloc;
    QVERIFY(bloc.getAiKeyClues().contains(QLatin1String(PageBlocMeta::KEY_SEO_DESC)));
}

void Test_Website_PageBlocMeta::test_meta_ai_key_clues_values_non_empty()
{
    PageBlocMeta bloc;
    const auto clues = bloc.getAiKeyClues();
    for (const QString &v : clues) {
        QVERIFY(!v.isEmpty());
    }
}

// =============================================================================
// createEditWidget
// =============================================================================

void Test_Website_PageBlocMeta::test_meta_create_edit_widget_returns_non_null()
{
    PageBlocMeta bloc;
    std::unique_ptr<AbstractPageBlockWidget> w(bloc.createEditWidget());
    QVERIFY(w != nullptr);
}

// =============================================================================
// addCode — no-op
// =============================================================================

void Test_Website_PageBlocMeta::test_meta_addcode_emits_no_html()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QStringLiteral("My Title"), QStringLiteral("My description.")));
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.isEmpty());
}

void Test_Website_PageBlocMeta::test_meta_addcode_emits_no_css()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QStringLiteral("Title"), QStringLiteral("Desc")));
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(css.isEmpty());
}

void Test_Website_PageBlocMeta::test_meta_addcode_emits_no_js()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QStringLiteral("Title"), QStringLiteral("Desc")));
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(js.isEmpty());
}

// =============================================================================
// load / save
// =============================================================================

void Test_Website_PageBlocMeta::test_meta_load_save_roundtrip_title()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QStringLiteral("SEO Title Here"), QString()));
    QHash<QString, QString> saved;
    bloc.save(saved);
    QCOMPARE(saved.value(QLatin1String(PageBlocMeta::KEY_SEO_TITLE)),
             QStringLiteral("SEO Title Here"));
}

void Test_Website_PageBlocMeta::test_meta_load_save_roundtrip_desc()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QString(), QStringLiteral("My meta description.")));
    QHash<QString, QString> saved;
    bloc.save(saved);
    QCOMPARE(saved.value(QLatin1String(PageBlocMeta::KEY_SEO_DESC)),
             QStringLiteral("My meta description."));
}

void Test_Website_PageBlocMeta::test_meta_load_empty_hash_produces_empty_values()
{
    PageBlocMeta bloc;
    bloc.load({});
    QVERIFY(bloc.seoTitle(QString()).isEmpty());
    QVERIFY(bloc.seoDescription(QString()).isEmpty());
}

void Test_Website_PageBlocMeta::test_meta_save_contains_title_key()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QStringLiteral("T"), QStringLiteral("D")));
    QHash<QString, QString> saved;
    bloc.save(saved);
    QVERIFY(saved.contains(QLatin1String(PageBlocMeta::KEY_SEO_TITLE)));
}

void Test_Website_PageBlocMeta::test_meta_save_contains_desc_key()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QStringLiteral("T"), QStringLiteral("D")));
    QHash<QString, QString> saved;
    bloc.save(saved);
    QVERIFY(saved.contains(QLatin1String(PageBlocMeta::KEY_SEO_DESC)));
}

// =============================================================================
// Accessors: source fallback
// =============================================================================

void Test_Website_PageBlocMeta::test_meta_seo_title_returns_source_when_no_translation()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QStringLiteral("Source Title"), QString()));
    QCOMPARE(bloc.seoTitle(QStringLiteral("fr")), QStringLiteral("Source Title"));
}

void Test_Website_PageBlocMeta::test_meta_seo_desc_returns_source_when_no_translation()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QString(), QStringLiteral("Source desc.")));
    QCOMPARE(bloc.seoDescription(QStringLiteral("fr")), QStringLiteral("Source desc."));
}

void Test_Website_PageBlocMeta::test_meta_seo_title_returns_translation_when_available()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QStringLiteral("Source Title"), QString()));
    bloc.applyTranslation(QStringView{}, QLatin1String(PageBlocMeta::KEY_SEO_TITLE),
                          QStringLiteral("fr"), QStringLiteral("Titre SEO"));
    QCOMPARE(bloc.seoTitle(QStringLiteral("fr")), QStringLiteral("Titre SEO"));
}

void Test_Website_PageBlocMeta::test_meta_seo_desc_returns_translation_when_available()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QString(), QStringLiteral("Source desc.")));
    bloc.applyTranslation(QStringView{}, QLatin1String(PageBlocMeta::KEY_SEO_DESC),
                          QStringLiteral("fr"), QStringLiteral("Description FR."));
    QCOMPARE(bloc.seoDescription(QStringLiteral("fr")), QStringLiteral("Description FR."));
}

void Test_Website_PageBlocMeta::test_meta_seo_title_empty_when_not_loaded()
{
    PageBlocMeta bloc;
    QVERIFY(bloc.seoTitle(QStringLiteral("en")).isEmpty());
}

void Test_Website_PageBlocMeta::test_meta_seo_desc_empty_when_not_loaded()
{
    PageBlocMeta bloc;
    QVERIFY(bloc.seoDescription(QStringLiteral("en")).isEmpty());
}

// =============================================================================
// Translation protocol
// =============================================================================

void Test_Website_PageBlocMeta::test_meta_collect_translatables_empty_when_no_content()
{
    PageBlocMeta bloc;
    bloc.load({});
    QList<TranslatableField> fields;
    bloc.collectTranslatables(QStringView{}, fields);
    QVERIFY(fields.isEmpty());
}

void Test_Website_PageBlocMeta::test_meta_collect_translatables_includes_title_when_set()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QStringLiteral("Title"), QString()));
    QList<TranslatableField> fields;
    bloc.collectTranslatables(QStringView{}, fields);
    const bool hasTitleField = std::any_of(fields.cbegin(), fields.cend(), [](const TranslatableField &f) {
        return f.id == QLatin1String(PageBlocMeta::KEY_SEO_TITLE);
    });
    QVERIFY(hasTitleField);
}

void Test_Website_PageBlocMeta::test_meta_collect_translatables_includes_desc_when_set()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QString(), QStringLiteral("Desc")));
    QList<TranslatableField> fields;
    bloc.collectTranslatables(QStringView{}, fields);
    const bool hasDescField = std::any_of(fields.cbegin(), fields.cend(), [](const TranslatableField &f) {
        return f.id == QLatin1String(PageBlocMeta::KEY_SEO_DESC);
    });
    QVERIFY(hasDescField);
}

void Test_Website_PageBlocMeta::test_meta_collect_translatables_two_fields_both_non_empty()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QStringLiteral("T"), QStringLiteral("D")));
    QList<TranslatableField> fields;
    bloc.collectTranslatables(QStringView{}, fields);
    QCOMPARE(fields.size(), 2);
}

void Test_Website_PageBlocMeta::test_meta_apply_translation_updates_seo_title()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QStringLiteral("EN Title"), QString()));
    bloc.applyTranslation(QStringView{}, QLatin1String(PageBlocMeta::KEY_SEO_TITLE),
                          QStringLiteral("de"), QStringLiteral("DE Titel"));
    QCOMPARE(bloc.seoTitle(QStringLiteral("de")), QStringLiteral("DE Titel"));
    QCOMPARE(bloc.seoTitle(QStringLiteral("en")), QStringLiteral("EN Title"));
}

void Test_Website_PageBlocMeta::test_meta_apply_translation_updates_seo_desc()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QString(), QStringLiteral("EN Desc")));
    bloc.applyTranslation(QStringView{}, QLatin1String(PageBlocMeta::KEY_SEO_DESC),
                          QStringLiteral("de"), QStringLiteral("DE Beschreibung"));
    QCOMPARE(bloc.seoDescription(QStringLiteral("de")), QStringLiteral("DE Beschreibung"));
    QCOMPARE(bloc.seoDescription(QStringLiteral("en")), QStringLiteral("EN Desc"));
}

void Test_Website_PageBlocMeta::test_meta_is_translation_complete_false_before_any()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QStringLiteral("Title"), QStringLiteral("Desc")));
    QVERIFY(!bloc.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_Website_PageBlocMeta::test_meta_is_translation_complete_true_after_both()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QStringLiteral("Title"), QStringLiteral("Desc")));
    bloc.applyTranslation(QStringView{}, QLatin1String(PageBlocMeta::KEY_SEO_TITLE),
                          QStringLiteral("fr"), QStringLiteral("Titre"));
    bloc.applyTranslation(QStringView{}, QLatin1String(PageBlocMeta::KEY_SEO_DESC),
                          QStringLiteral("fr"), QStringLiteral("Description."));
    QVERIFY(bloc.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

void Test_Website_PageBlocMeta::test_meta_is_translation_complete_false_after_only_title()
{
    PageBlocMeta bloc;
    bloc.load(makeHash(QStringLiteral("Title"), QStringLiteral("Desc")));
    bloc.applyTranslation(QStringView{}, QLatin1String(PageBlocMeta::KEY_SEO_TITLE),
                          QStringLiteral("fr"), QStringLiteral("Titre"));
    QVERIFY(!bloc.isTranslationComplete(QStringView{}, QStringLiteral("fr")));
}

QTEST_MAIN(Test_Website_PageBlocMeta)
#include "test_website_page_bloc_meta.moc"
