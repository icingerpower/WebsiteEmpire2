#include <QtTest>

#include <QFile>
#include <QHash>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>

#include "website/pages/blocs/widgets/AbstractPageBlockWidget.h"
#include "website/pages/blocs/PageBlocCategoryArticles.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PermalinkHistoryEntry.h"
#include "website/EngineArticles.h"
#include "website/HostTable.h"

// =============================================================================
// FakePageRepository
// =============================================================================

/**
 * In-memory IPageRepository used in tests.  Only findAll() and loadData() are
 * implemented; all mutating methods and queries unused by the bloc are stubs.
 */
class FakePageRepository : public IPageRepository
{
public:
    void addPage(const PageRecord &record, const QHash<QString, QString> &data)
    {
        m_records.append(record);
        m_data.insert(record.id, data);
    }

    // ── Required by bloc ──────────────────────────────────────────────────────
    QList<PageRecord> findAll() const override { return m_records; }

    QHash<QString, QString> loadData(int id) const override
    {
        return m_data.value(id);
    }

    // ── Unused stubs ──────────────────────────────────────────────────────────
    int create(const QString &, const QString &, const QString &) override { return 0; }
    int createTranslation(int, const QString &, const QString &, const QString &) override { return 0; }
    std::optional<PageRecord> findById(int) const override { return {}; }
    QList<PageRecord> findSourcePages(const QString &) const override { return {}; }
    QList<PageRecord> findTranslations(int) const override { return {}; }
    void saveData(int, const QHash<QString, QString> &) override {}
    void remove(int) override {}
    void updatePermalink(int, const QString &) override {}
    QList<PermalinkHistoryEntry> permalinkHistory(int) const override { return {}; }
    void setHistoryRedirectType(int, const QString &) override {}
    QString translatedAt(int) const override { return {}; }
    void setTranslatedAt(int, const QString &) override {}
    QList<PageRecord> findPendingByTypeId(const QString &) const override { return {}; }
    int countByTypeId(const QString &) const override { return 0; }
    void setGeneratedAt(int, const QString &) override {}
    void recordStrategyAttempt(int, const QString &) override {}
    QStringList strategyAttempts(int) const override { return {}; }
    QList<PageRecord> findGeneratedByTypeId(const QString &) const override { return {}; }
    void setLangCodesToTranslate(int, const QStringList &) override {}
    void clearTranslationData(int, const QString &) override {}
    void clearAllTranslationData(int) override {}

private:
    QList<PageRecord>                    m_records;
    QHash<int, QHash<QString, QString>>  m_data;
};

// =============================================================================
// Helpers
// =============================================================================

namespace {

// Engine used in addCode calls (not used by the bloc itself).
EngineArticles engine;

// Builds a PageRecord for a source article.
PageRecord makeArticle(int id,
                       const QString &permalink,
                       const QString &createdAt = QStringLiteral("2024-01-01T00:00:00Z"),
                       const QString &updatedAt = QStringLiteral("2024-06-01T00:00:00Z"))
{
    PageRecord r;
    r.id           = id;
    r.typeId       = QStringLiteral("article");
    r.permalink    = permalink;
    r.lang         = QStringLiteral("en");
    r.createdAt    = createdAt;
    r.updatedAt    = updatedAt;
    r.sourcePageId = 0;
    return r;
}

// Article data with one category.
QHash<QString, QString> articleData(int categoryId)
{
    return {{QStringLiteral("0_categories"), QString::number(categoryId)}};
}

// Article data with multiple categories.
QHash<QString, QString> articleDataMultiCat(const QList<int> &categoryIds)
{
    QStringList parts;
    for (int id : categoryIds) {
        parts.append(QString::number(id));
    }
    return {{QStringLiteral("0_categories"), parts.join(QLatin1Char(','))}};
}

// Build a hash with one entry.
QHash<QString, QString> oneEntryHash(int categoryId,
                                     const QString &title,
                                     int itemCount,
                                     const QString &sort)
{
    QHash<QString, QString> h;
    h.insert(QLatin1String(PageBlocCategoryArticles::KEY_ENTRY_COUNT), QStringLiteral("1"));
    h.insert(QStringLiteral("entry_0_cat_id"),    QString::number(categoryId));
    h.insert(QStringLiteral("entry_0_title"),     title);
    h.insert(QStringLiteral("entry_0_item_count"),QString::number(itemCount));
    h.insert(QStringLiteral("entry_0_sort"),      sort);
    return h;
}

// Run addCode; returns html.
QString htmlFrom(PageBlocCategoryArticles &bloc,
                 const QHash<QString, QString> &values = {})
{
    bloc.load(values);
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    return html;
}

// Run addCode; returns css.
QString cssFrom(PageBlocCategoryArticles &bloc,
                const QHash<QString, QString> &values = {})
{
    bloc.load(values);
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    return css;
}

// Run addCode; returns js.
QString jsFrom(PageBlocCategoryArticles &bloc,
               const QHash<QString, QString> &values = {})
{
    bloc.load(values);
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    return js;
}

} // namespace

// =============================================================================
// Test_Website_PageBlocCategoryArticles
// =============================================================================

class Test_Website_PageBlocCategoryArticles : public QObject
{
    Q_OBJECT

private slots:
    // --- per-test setup ---
    void init();
    void cleanup();

    // --- createEditWidget ---
    void test_catart_create_edit_widget_returns_non_null();
    void test_catart_create_edit_widget_has_no_parent();

    // --- getName ---
    void test_catart_get_name_is_not_empty();

    // --- Empty bloc ---
    void test_catart_no_entries_produces_no_html();
    void test_catart_no_entries_produces_no_css();
    void test_catart_no_entries_produces_no_js();
    void test_catart_no_entries_leaves_existing_html_unchanged();

    // --- Category with no articles ---
    void test_catart_category_no_articles_produces_no_html();
    void test_catart_category_no_articles_produces_no_css();
    void test_catart_category_no_articles_produces_no_js();

    // --- Single category with articles: HTML structure ---
    void test_catart_single_entry_produces_wrapper_div();
    void test_catart_single_entry_produces_section_tag();
    void test_catart_single_entry_produces_h2_tag();
    void test_catart_single_entry_title_in_h2();
    void test_catart_single_entry_produces_ul_tag();
    void test_catart_single_entry_article_link_present();
    void test_catart_single_entry_article_href_is_permalink();
    void test_catart_single_entry_title_derived_from_permalink();

    // --- data-* attributes for tracking ---
    void test_catart_section_has_data_section_id();
    void test_catart_section_id_is_entry_index();
    void test_catart_links_have_data_link_id();
    void test_catart_link_id_format_entry_dash_article();

    // --- itemCount limit ---
    void test_catart_item_count_limits_articles();
    void test_catart_item_count_does_not_exceed_available_articles();

    // --- Sort: recent ---
    void test_catart_sort_recent_newest_updatedAt_first();

    // --- Sort: performance ---
    void test_catart_sort_performance_oldest_createdAt_first();

    // --- Sort: combined ---
    void test_catart_sort_combined_interleaves_unique_articles();
    void test_catart_sort_combined_no_duplicates();

    // --- Sort: unknown (fallback) ---
    void test_catart_unknown_sort_order_falls_back_to_recent();

    // --- Multiple entries ---
    void test_catart_two_entries_both_produce_sections();
    void test_catart_two_entries_section_ids_match_entry_indices();
    void test_catart_entry_with_no_articles_not_in_html();

    // --- Title fallback ---
    void test_catart_empty_entry_title_falls_back_to_category_name();

    // --- Permalink to title ---
    void test_catart_permalink_title_strips_leading_slash();
    void test_catart_permalink_title_strips_html_extension();
    void test_catart_permalink_title_replaces_hyphens_with_spaces();
    void test_catart_permalink_title_capitalizes_each_word();

    // --- CSS ---
    void test_catart_css_emitted_when_articles_present();
    void test_catart_css_emitted_only_once();
    void test_catart_css_contains_cat_art_bloc_class();
    void test_catart_css_contains_cat_art_section_class();

    // --- JS ---
    void test_catart_js_emitted_when_articles_present();
    void test_catart_js_emitted_only_once();
    void test_catart_js_contains_intersection_observer();
    void test_catart_js_contains_send_beacon();
    void test_catart_js_contains_ca_section_display();
    void test_catart_js_contains_ca_link_click();

    // --- Append behaviour ---
    void test_catart_appends_to_existing_html();
    void test_catart_appends_css_to_existing_css();

    // --- Translations (source page filter) ---
    void test_catart_translation_pages_excluded();

    // --- load / save ---
    void test_catart_load_save_roundtrip_entry_count();
    void test_catart_load_save_roundtrip_cat_id();
    void test_catart_load_save_roundtrip_title();
    void test_catart_load_save_roundtrip_item_count();
    void test_catart_load_save_roundtrip_sort();
    void test_catart_load_ignores_unknown_keys();
    void test_catart_load_empty_hash_gives_no_output();
    void test_catart_save_writes_entry_count();

    // --- entries accessor ---
    void test_catart_entries_accessor_matches_loaded_data();
    void test_catart_entries_empty_after_default_construction();

private:
    QTemporaryDir       *m_tmpDir  = nullptr;
    CategoryTable       *m_catTable = nullptr;
    FakePageRepository  *m_repo     = nullptr;
};

// =============================================================================
// Setup / teardown
// =============================================================================

void Test_Website_PageBlocCategoryArticles::init()
{
    m_tmpDir   = new QTemporaryDir;
    m_catTable = new CategoryTable(QDir(m_tmpDir->path()));
    m_repo     = new FakePageRepository;
}

void Test_Website_PageBlocCategoryArticles::cleanup()
{
    delete m_catTable;
    m_catTable = nullptr;
    delete m_tmpDir;
    m_tmpDir   = nullptr;
    delete m_repo;
    m_repo     = nullptr;
}

// =============================================================================
// createEditWidget
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_create_edit_widget_returns_non_null()
{
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    AbstractPageBlockWidget *w = bloc.createEditWidget();
    QVERIFY(w != nullptr);   // 1
    delete w;
}

void Test_Website_PageBlocCategoryArticles::test_catart_create_edit_widget_has_no_parent()
{
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    AbstractPageBlockWidget *w = bloc.createEditWidget();
    QVERIFY(w->parent() == nullptr);   // 2
    delete w;
}

// =============================================================================
// getName
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_get_name_is_not_empty()
{
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    QVERIFY(!bloc.getName().isEmpty());   // 3
}

// =============================================================================
// Empty bloc
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_no_entries_produces_no_html()
{
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    QVERIFY(htmlFrom(bloc).isEmpty());   // 4
}

void Test_Website_PageBlocCategoryArticles::test_catart_no_entries_produces_no_css()
{
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    QVERIFY(cssFrom(bloc).isEmpty());   // 5
}

void Test_Website_PageBlocCategoryArticles::test_catart_no_entries_produces_no_js()
{
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    QVERIFY(jsFrom(bloc).isEmpty());   // 6
}

void Test_Website_PageBlocCategoryArticles::test_catart_no_entries_leaves_existing_html_unchanged()
{
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    bloc.load({});
    QString html = QStringLiteral("pre");
    QString css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(html, QStringLiteral("pre"));   // 7
}

// =============================================================================
// Category with no articles
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_category_no_articles_produces_no_html()
{
    // Repo has no articles at all.
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    QVERIFY(htmlFrom(bloc, oneEntryHash(1, QStringLiteral("Electronics"), 5,
                                        QLatin1String(PageBlocCategoryArticles::SORT_RECENT))).isEmpty());   // 8
}

void Test_Website_PageBlocCategoryArticles::test_catart_category_no_articles_produces_no_css()
{
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    QVERIFY(cssFrom(bloc, oneEntryHash(1, QStringLiteral("Electronics"), 5,
                                       QLatin1String(PageBlocCategoryArticles::SORT_RECENT))).isEmpty());   // 9
}

void Test_Website_PageBlocCategoryArticles::test_catart_category_no_articles_produces_no_js()
{
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    QVERIFY(jsFrom(bloc, oneEntryHash(1, QStringLiteral("Electronics"), 5,
                                      QLatin1String(PageBlocCategoryArticles::SORT_RECENT))).isEmpty());   // 10
}

// =============================================================================
// Single category with articles: HTML structure
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_single_entry_produces_wrapper_div()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/my-article.html")), articleData(3));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(3, QStringLiteral("Tech"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(html.contains(QStringLiteral("<div class=\"cat-art-bloc\">")));   // 11
    QVERIFY(html.contains(QStringLiteral("</div>")));   // 12
}

void Test_Website_PageBlocCategoryArticles::test_catart_single_entry_produces_section_tag()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/my-article.html")), articleData(3));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(3, QStringLiteral("Tech"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(html.contains(QStringLiteral("<section")));   // 13
    QVERIFY(html.contains(QStringLiteral("</section>")));   // 14
}

void Test_Website_PageBlocCategoryArticles::test_catart_single_entry_produces_h2_tag()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/my-article.html")), articleData(3));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(3, QStringLiteral("Tech"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(html.contains(QStringLiteral("<h2")));   // 15
}

void Test_Website_PageBlocCategoryArticles::test_catart_single_entry_title_in_h2()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/my-article.html")), articleData(3));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(3, QStringLiteral("Tech News"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(html.contains(QStringLiteral("Tech News")));   // 16
}

void Test_Website_PageBlocCategoryArticles::test_catart_single_entry_produces_ul_tag()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/my-article.html")), articleData(3));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(3, QStringLiteral("Tech"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(html.contains(QStringLiteral("<ul")));   // 17
}

void Test_Website_PageBlocCategoryArticles::test_catart_single_entry_article_link_present()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/my-article.html")), articleData(3));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(3, QStringLiteral("Tech"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(html.contains(QStringLiteral("<a ")));   // 18
}

void Test_Website_PageBlocCategoryArticles::test_catart_single_entry_article_href_is_permalink()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/my-article.html")), articleData(3));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(3, QStringLiteral("Tech"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(html.contains(QStringLiteral("href=\"/my-article.html\"")));   // 19
}

void Test_Website_PageBlocCategoryArticles::test_catart_single_entry_title_derived_from_permalink()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/my-yoga-class.html")), articleData(3));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(3, QStringLiteral("Yoga"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    // /my-yoga-class.html -> My Yoga Class
    QVERIFY(html.contains(QStringLiteral("My Yoga Class")));   // 20
}

// =============================================================================
// data-* attributes for tracking
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_section_has_data_section_id()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/art.html")), articleData(3));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(3, QStringLiteral("T"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(html.contains(QStringLiteral("data-section-id=")));   // 21
}

void Test_Website_PageBlocCategoryArticles::test_catart_section_id_is_entry_index()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/art.html")), articleData(3));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    // Single entry at index 0
    const auto &html = htmlFrom(bloc, oneEntryHash(3, QStringLiteral("T"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(html.contains(QStringLiteral("data-section-id=\"0\"")));   // 22
}

void Test_Website_PageBlocCategoryArticles::test_catart_links_have_data_link_id()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/art.html")), articleData(3));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(3, QStringLiteral("T"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(html.contains(QStringLiteral("data-link-id=")));   // 23
}

void Test_Website_PageBlocCategoryArticles::test_catart_link_id_format_entry_dash_article()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/art.html")), articleData(3));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(3, QStringLiteral("T"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    // First entry (0), first article (0) -> "0-0"
    QVERIFY(html.contains(QStringLiteral("data-link-id=\"0-0\"")));   // 24
}

// =============================================================================
// itemCount limit
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_item_count_limits_articles()
{
    for (int i = 1; i <= 10; ++i) {
        const auto &slug = QStringLiteral("/article-") + QString::number(i) + QStringLiteral(".html");
        m_repo->addPage(makeArticle(i, slug), articleData(7));
    }
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(7, QStringLiteral("Cat"), 3,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QCOMPARE(html.count(QStringLiteral("<li")), 3);   // 25
}

void Test_Website_PageBlocCategoryArticles::test_catart_item_count_does_not_exceed_available_articles()
{
    // Repo has only 2 articles but itemCount = 10
    m_repo->addPage(makeArticle(1, QStringLiteral("/a.html")), articleData(7));
    m_repo->addPage(makeArticle(2, QStringLiteral("/b.html")), articleData(7));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(7, QStringLiteral("Cat"), 10,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QCOMPARE(html.count(QStringLiteral("<li")), 2);   // 26
}

// =============================================================================
// Sort: recent
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_sort_recent_newest_updatedAt_first()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/old.html"),
                                QStringLiteral("2024-01-01T00:00:00Z"),
                                QStringLiteral("2024-01-15T00:00:00Z")), articleData(5));
    m_repo->addPage(makeArticle(2, QStringLiteral("/new.html"),
                                QStringLiteral("2024-03-01T00:00:00Z"),
                                QStringLiteral("2024-09-01T00:00:00Z")), articleData(5));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(5, QStringLiteral("Cat"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    // Newest updatedAt ("/new.html") must appear before oldest ("/old.html")
    QVERIFY(html.indexOf(QStringLiteral("/new.html")) < html.indexOf(QStringLiteral("/old.html")));   // 27
}

// =============================================================================
// Sort: performance
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_sort_performance_oldest_createdAt_first()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/younger.html"),
                                QStringLiteral("2024-06-01T00:00:00Z"),
                                QStringLiteral("2024-09-01T00:00:00Z")), articleData(5));
    m_repo->addPage(makeArticle(2, QStringLiteral("/older.html"),
                                QStringLiteral("2023-01-01T00:00:00Z"),
                                QStringLiteral("2023-06-01T00:00:00Z")), articleData(5));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(5, QStringLiteral("Cat"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_PERFORMANCE)));
    // Oldest createdAt ("/older.html") must appear before ("/younger.html")
    QVERIFY(html.indexOf(QStringLiteral("/older.html")) < html.indexOf(QStringLiteral("/younger.html")));   // 28
}

// =============================================================================
// Sort: combined
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_sort_combined_interleaves_unique_articles()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/a.html"),
                                QStringLiteral("2023-01-01T00:00:00Z"),
                                QStringLiteral("2024-09-01T00:00:00Z")), articleData(5));
    m_repo->addPage(makeArticle(2, QStringLiteral("/b.html"),
                                QStringLiteral("2022-01-01T00:00:00Z"),
                                QStringLiteral("2024-01-01T00:00:00Z")), articleData(5));
    m_repo->addPage(makeArticle(3, QStringLiteral("/c.html"),
                                QStringLiteral("2024-01-01T00:00:00Z"),
                                QStringLiteral("2023-01-01T00:00:00Z")), articleData(5));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(5, QStringLiteral("Cat"), 3,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_COMBINED)));
    // All 3 articles must appear
    QVERIFY(html.contains(QStringLiteral("/a.html")));   // 29
    QVERIFY(html.contains(QStringLiteral("/b.html")));   // 30
    QVERIFY(html.contains(QStringLiteral("/c.html")));   // 31
}

void Test_Website_PageBlocCategoryArticles::test_catart_sort_combined_no_duplicates()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/a.html"),
                                QStringLiteral("2023-01-01T00:00:00Z"),
                                QStringLiteral("2024-09-01T00:00:00Z")), articleData(5));
    m_repo->addPage(makeArticle(2, QStringLiteral("/b.html"),
                                QStringLiteral("2022-01-01T00:00:00Z"),
                                QStringLiteral("2024-01-01T00:00:00Z")), articleData(5));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(5, QStringLiteral("Cat"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_COMBINED)));
    // Only 2 articles exist; must appear exactly once each.
    QCOMPARE(html.count(QStringLiteral("/a.html")), 1);   // 32
    QCOMPARE(html.count(QStringLiteral("/b.html")), 1);   // 33
}

// =============================================================================
// Sort: unknown (fallback)
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_unknown_sort_order_falls_back_to_recent()
{
    // An unrecognised sort value must not crash or produce empty output:
    // the bloc falls back to the "recent" (updatedAt descending) order.
    m_repo->addPage(makeArticle(1, QStringLiteral("/older.html"),
                                QStringLiteral("2023-01-01T00:00:00Z"),
                                QStringLiteral("2024-01-01T00:00:00Z")), articleData(7));
    m_repo->addPage(makeArticle(2, QStringLiteral("/newer.html"),
                                QStringLiteral("2023-01-01T00:00:00Z"),
                                QStringLiteral("2024-09-01T00:00:00Z")), articleData(7));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(7, QStringLiteral("Cat"), 5,
                                                    QStringLiteral("unknown_sort")));
    // Both articles must appear exactly once.
    QCOMPARE(html.count(QStringLiteral("/older.html")), 1);
    QCOMPARE(html.count(QStringLiteral("/newer.html")), 1);
    // Fallback is "recent": newer updatedAt should come first.
    QVERIFY(html.indexOf(QStringLiteral("/newer.html")) < html.indexOf(QStringLiteral("/older.html")));
}

// =============================================================================
// Multiple entries
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_two_entries_both_produce_sections()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/yoga.html")), articleData(1));
    m_repo->addPage(makeArticle(2, QStringLiteral("/tech.html")), articleData(2));

    QHash<QString, QString> h;
    h.insert(QLatin1String(PageBlocCategoryArticles::KEY_ENTRY_COUNT), QStringLiteral("2"));
    h.insert(QStringLiteral("entry_0_cat_id"),     QStringLiteral("1"));
    h.insert(QStringLiteral("entry_0_title"),      QStringLiteral("Yoga"));
    h.insert(QStringLiteral("entry_0_item_count"), QStringLiteral("5"));
    h.insert(QStringLiteral("entry_0_sort"),       QLatin1String(PageBlocCategoryArticles::SORT_RECENT));
    h.insert(QStringLiteral("entry_1_cat_id"),     QStringLiteral("2"));
    h.insert(QStringLiteral("entry_1_title"),      QStringLiteral("Tech"));
    h.insert(QStringLiteral("entry_1_item_count"), QStringLiteral("5"));
    h.insert(QStringLiteral("entry_1_sort"),       QLatin1String(PageBlocCategoryArticles::SORT_RECENT));

    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, h);
    QCOMPARE(html.count(QStringLiteral("<section")), 2);   // 34
}

void Test_Website_PageBlocCategoryArticles::test_catart_two_entries_section_ids_match_entry_indices()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/yoga.html")), articleData(1));
    m_repo->addPage(makeArticle(2, QStringLiteral("/tech.html")), articleData(2));

    QHash<QString, QString> h;
    h.insert(QLatin1String(PageBlocCategoryArticles::KEY_ENTRY_COUNT), QStringLiteral("2"));
    h.insert(QStringLiteral("entry_0_cat_id"),     QStringLiteral("1"));
    h.insert(QStringLiteral("entry_0_title"),      QStringLiteral("Yoga"));
    h.insert(QStringLiteral("entry_0_item_count"), QStringLiteral("5"));
    h.insert(QStringLiteral("entry_0_sort"),       QLatin1String(PageBlocCategoryArticles::SORT_RECENT));
    h.insert(QStringLiteral("entry_1_cat_id"),     QStringLiteral("2"));
    h.insert(QStringLiteral("entry_1_title"),      QStringLiteral("Tech"));
    h.insert(QStringLiteral("entry_1_item_count"), QStringLiteral("5"));
    h.insert(QStringLiteral("entry_1_sort"),       QLatin1String(PageBlocCategoryArticles::SORT_RECENT));

    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, h);
    QVERIFY(html.contains(QStringLiteral("data-section-id=\"0\"")));   // 35
    QVERIFY(html.contains(QStringLiteral("data-section-id=\"1\"")));   // 36
}

void Test_Website_PageBlocCategoryArticles::test_catart_entry_with_no_articles_not_in_html()
{
    // Entry 0: category 1 has articles; entry 1: category 2 has none.
    m_repo->addPage(makeArticle(1, QStringLiteral("/yoga.html")), articleData(1));

    QHash<QString, QString> h;
    h.insert(QLatin1String(PageBlocCategoryArticles::KEY_ENTRY_COUNT), QStringLiteral("2"));
    h.insert(QStringLiteral("entry_0_cat_id"),     QStringLiteral("1"));
    h.insert(QStringLiteral("entry_0_title"),      QStringLiteral("Yoga"));
    h.insert(QStringLiteral("entry_0_item_count"), QStringLiteral("5"));
    h.insert(QStringLiteral("entry_0_sort"),       QLatin1String(PageBlocCategoryArticles::SORT_RECENT));
    h.insert(QStringLiteral("entry_1_cat_id"),     QStringLiteral("2"));
    h.insert(QStringLiteral("entry_1_title"),      QStringLiteral("Empty"));
    h.insert(QStringLiteral("entry_1_item_count"), QStringLiteral("5"));
    h.insert(QStringLiteral("entry_1_sort"),       QLatin1String(PageBlocCategoryArticles::SORT_RECENT));

    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, h);
    QCOMPARE(html.count(QStringLiteral("<section")), 1);   // 37
    QVERIFY(!html.contains(QStringLiteral("Empty")));      // 38
}

// =============================================================================
// Title fallback
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_empty_entry_title_falls_back_to_category_name()
{
    const int catId = m_catTable->addCategory(QStringLiteral("Wellness"));
    m_repo->addPage(makeArticle(1, QStringLiteral("/art.html")), articleData(catId));

    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(catId, QStringLiteral("") /* empty title */,
                                                    5, QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(html.contains(QStringLiteral("Wellness")));   // 39
}

// =============================================================================
// Permalink to title
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_permalink_title_strips_leading_slash()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/article.html")), articleData(1));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(1, QStringLiteral("Cat"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(!html.contains(QStringLiteral(">/article")));   // 40 — no leading slash in link text
}

void Test_Website_PageBlocCategoryArticles::test_catart_permalink_title_strips_html_extension()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/hello.html")), articleData(1));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(1, QStringLiteral("Cat"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(!html.contains(QStringLiteral(".html</a>")));   // 41
}

void Test_Website_PageBlocCategoryArticles::test_catart_permalink_title_replaces_hyphens_with_spaces()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/hello-world.html")), articleData(1));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(1, QStringLiteral("Cat"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(html.contains(QStringLiteral("Hello World")));   // 42
}

void Test_Website_PageBlocCategoryArticles::test_catart_permalink_title_capitalizes_each_word()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/foo-bar-baz.html")), articleData(1));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &html = htmlFrom(bloc, oneEntryHash(1, QStringLiteral("Cat"), 5,
                                                    QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(html.contains(QStringLiteral("Foo Bar Baz")));   // 43
}

// =============================================================================
// CSS
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_css_emitted_when_articles_present()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/art.html")), articleData(1));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    QVERIFY(!cssFrom(bloc, oneEntryHash(1, QStringLiteral("C"), 5,
                                        QLatin1String(PageBlocCategoryArticles::SORT_RECENT))).isEmpty());   // 44
}

void Test_Website_PageBlocCategoryArticles::test_catart_css_emitted_only_once()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/art.html")), articleData(1));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    bloc.load(oneEntryHash(1, QStringLiteral("C"), 5, QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));

    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    const int len = css.length();
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(css.length(), len);   // 45
}

void Test_Website_PageBlocCategoryArticles::test_catart_css_contains_cat_art_bloc_class()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/art.html")), articleData(1));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &css = cssFrom(bloc, oneEntryHash(1, QStringLiteral("C"), 5,
                                                  QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(css.contains(QStringLiteral(".cat-art-bloc")));   // 46
}

void Test_Website_PageBlocCategoryArticles::test_catart_css_contains_cat_art_section_class()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/art.html")), articleData(1));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &css = cssFrom(bloc, oneEntryHash(1, QStringLiteral("C"), 5,
                                                  QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(css.contains(QStringLiteral(".cat-art-section")));   // 47
}

// =============================================================================
// JS
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_js_emitted_when_articles_present()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/art.html")), articleData(1));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    QVERIFY(!jsFrom(bloc, oneEntryHash(1, QStringLiteral("C"), 5,
                                       QLatin1String(PageBlocCategoryArticles::SORT_RECENT))).isEmpty());   // 48
}

void Test_Website_PageBlocCategoryArticles::test_catart_js_emitted_only_once()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/art.html")), articleData(1));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    bloc.load(oneEntryHash(1, QStringLiteral("C"), 5, QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));

    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    const int len = js.length();
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(js.length(), len);   // 49
}

void Test_Website_PageBlocCategoryArticles::test_catart_js_contains_intersection_observer()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/art.html")), articleData(1));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &js = jsFrom(bloc, oneEntryHash(1, QStringLiteral("C"), 5,
                                                QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(js.contains(QStringLiteral("IntersectionObserver")));   // 50
}

void Test_Website_PageBlocCategoryArticles::test_catart_js_contains_send_beacon()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/art.html")), articleData(1));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &js = jsFrom(bloc, oneEntryHash(1, QStringLiteral("C"), 5,
                                                QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(js.contains(QStringLiteral("sendBeacon")));   // 51
}

void Test_Website_PageBlocCategoryArticles::test_catart_js_contains_ca_section_display()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/art.html")), articleData(1));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &js = jsFrom(bloc, oneEntryHash(1, QStringLiteral("C"), 5,
                                                QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(js.contains(QStringLiteral("ca-section-display")));   // 52
}

void Test_Website_PageBlocCategoryArticles::test_catart_js_contains_ca_link_click()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/art.html")), articleData(1));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    const auto &js = jsFrom(bloc, oneEntryHash(1, QStringLiteral("C"), 5,
                                                QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QVERIFY(js.contains(QStringLiteral("ca-link-click")));   // 53
}

// =============================================================================
// Append behaviour
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_appends_to_existing_html()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/art.html")), articleData(1));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    bloc.load(oneEntryHash(1, QStringLiteral("C"), 5, QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QString html = QStringLiteral("existing");
    QString css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.startsWith(QStringLiteral("existing")));   // 54
    QVERIFY(html.contains(QStringLiteral("<div")));         // 55
}

void Test_Website_PageBlocCategoryArticles::test_catart_appends_css_to_existing_css()
{
    m_repo->addPage(makeArticle(1, QStringLiteral("/art.html")), articleData(1));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    bloc.load(oneEntryHash(1, QStringLiteral("C"), 5, QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QString html;
    QString css = QStringLiteral("pre-css");
    QString js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(css.startsWith(QStringLiteral("pre-css")));           // 56
    QVERIFY(css.contains(QStringLiteral(".cat-art-bloc")));       // 57
}

// =============================================================================
// Translations (source page filter)
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_translation_pages_excluded()
{
    // Since Task 9, filtering uses engine.isPageAvailable() rather than
    // sourcePageId.  Pages absent from the engine's available-pages map are
    // excluded from the listing.
    m_repo->addPage(makeArticle(1, QStringLiteral("/source.html")),    articleData(9));
    m_repo->addPage(makeArticle(2, QStringLiteral("/fr-source.html")), articleData(9));

    // Build a local engine with lang "en" at index 0, restricted to /source.html.
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
    EngineArticles localEngine;
    localEngine.init(QDir(dir.path()), hostTable);

    QHash<QString, QSet<QString>> map;
    map[QStringLiteral("fr")].insert(QStringLiteral("/source.html"));
    localEngine.setAvailablePages(map);

    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    bloc.load(oneEntryHash(9, QStringLiteral("Cat"), 10,
                           QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, localEngine, 0, html, css, js, cssDoneIds, jsDoneIds);

    QCOMPARE(html.count(QStringLiteral("<li")), 1);               // 58 — only /source.html
    QVERIFY(html.contains(QStringLiteral("/source.html")));       // 59
    QVERIFY(!html.contains(QStringLiteral("/fr-source.html")));   // 60
}

// =============================================================================
// load / save
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_load_save_roundtrip_entry_count()
{
    QHash<QString, QString> h;
    h.insert(QLatin1String(PageBlocCategoryArticles::KEY_ENTRY_COUNT), QStringLiteral("2"));
    h.insert(QStringLiteral("entry_0_cat_id"),     QStringLiteral("1"));
    h.insert(QStringLiteral("entry_0_title"),      QStringLiteral("A"));
    h.insert(QStringLiteral("entry_0_item_count"), QStringLiteral("3"));
    h.insert(QStringLiteral("entry_0_sort"),       QLatin1String(PageBlocCategoryArticles::SORT_RECENT));
    h.insert(QStringLiteral("entry_1_cat_id"),     QStringLiteral("2"));
    h.insert(QStringLiteral("entry_1_title"),      QStringLiteral("B"));
    h.insert(QStringLiteral("entry_1_item_count"), QStringLiteral("7"));
    h.insert(QStringLiteral("entry_1_sort"),       QLatin1String(PageBlocCategoryArticles::SORT_PERFORMANCE));

    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    bloc.load(h);
    QHash<QString, QString> saved;
    bloc.save(saved);
    QCOMPARE(saved.value(QLatin1String(PageBlocCategoryArticles::KEY_ENTRY_COUNT)), QStringLiteral("2"));   // 61
}

void Test_Website_PageBlocCategoryArticles::test_catart_load_save_roundtrip_cat_id()
{
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    bloc.load(oneEntryHash(42, QStringLiteral("T"), 5,
                           QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QHash<QString, QString> saved;
    bloc.save(saved);
    QCOMPARE(saved.value(QStringLiteral("entry_0_cat_id")), QStringLiteral("42"));   // 62
}

void Test_Website_PageBlocCategoryArticles::test_catart_load_save_roundtrip_title()
{
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    bloc.load(oneEntryHash(1, QStringLiteral("My Custom Title"), 5,
                           QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QHash<QString, QString> saved;
    bloc.save(saved);
    QCOMPARE(saved.value(QStringLiteral("entry_0_title")), QStringLiteral("My Custom Title"));   // 63
}

void Test_Website_PageBlocCategoryArticles::test_catart_load_save_roundtrip_item_count()
{
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    bloc.load(oneEntryHash(1, QStringLiteral("T"), 8,
                           QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QHash<QString, QString> saved;
    bloc.save(saved);
    QCOMPARE(saved.value(QStringLiteral("entry_0_item_count")), QStringLiteral("8"));   // 64
}

void Test_Website_PageBlocCategoryArticles::test_catart_load_save_roundtrip_sort()
{
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    bloc.load(oneEntryHash(1, QStringLiteral("T"), 5,
                           QLatin1String(PageBlocCategoryArticles::SORT_COMBINED)));
    QHash<QString, QString> saved;
    bloc.save(saved);
    QCOMPARE(saved.value(QStringLiteral("entry_0_sort")),
             QLatin1String(PageBlocCategoryArticles::SORT_COMBINED));   // 65
}

void Test_Website_PageBlocCategoryArticles::test_catart_load_ignores_unknown_keys()
{
    QHash<QString, QString> h = oneEntryHash(1, QStringLiteral("T"), 5,
                                             QLatin1String(PageBlocCategoryArticles::SORT_RECENT));
    h.insert(QStringLiteral("unknown_future_key"), QStringLiteral("value"));
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    bloc.load(h);   // must not throw
    QHash<QString, QString> saved;
    bloc.save(saved);
    QVERIFY(!saved.contains(QStringLiteral("unknown_future_key")));   // 66
}

void Test_Website_PageBlocCategoryArticles::test_catart_load_empty_hash_gives_no_output()
{
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    bloc.load({});
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.isEmpty());   // 67
    QVERIFY(css.isEmpty());    // 68
    QVERIFY(js.isEmpty());     // 69
}

void Test_Website_PageBlocCategoryArticles::test_catart_save_writes_entry_count()
{
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    bloc.load(oneEntryHash(1, QStringLiteral("T"), 5,
                           QLatin1String(PageBlocCategoryArticles::SORT_RECENT)));
    QHash<QString, QString> saved;
    bloc.save(saved);
    QVERIFY(saved.contains(QLatin1String(PageBlocCategoryArticles::KEY_ENTRY_COUNT)));   // 70
    QCOMPARE(saved.value(QLatin1String(PageBlocCategoryArticles::KEY_ENTRY_COUNT)),
             QStringLiteral("1"));   // 71
}

// =============================================================================
// entries accessor
// =============================================================================

void Test_Website_PageBlocCategoryArticles::test_catart_entries_accessor_matches_loaded_data()
{
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    bloc.load(oneEntryHash(7, QStringLiteral("Tech"), 3,
                           QLatin1String(PageBlocCategoryArticles::SORT_PERFORMANCE)));
    const auto &entries = bloc.entries();
    QCOMPARE(entries.size(), 1);   // 72
    QCOMPARE(entries.at(0).categoryId, 7);   // 73
    QCOMPARE(entries.at(0).title,      QStringLiteral("Tech"));   // 74
    QCOMPARE(entries.at(0).itemCount,  3);   // 75
    QCOMPARE(entries.at(0).sortOrder,  QLatin1String(PageBlocCategoryArticles::SORT_PERFORMANCE));   // 76
}

void Test_Website_PageBlocCategoryArticles::test_catart_entries_empty_after_default_construction()
{
    PageBlocCategoryArticles bloc(*m_catTable, *m_repo);
    QVERIFY(bloc.entries().isEmpty());   // 77
}

QTEST_MAIN(Test_Website_PageBlocCategoryArticles)
#include "test_website_page_bloc_image_category.moc"
