#include <QtTest>

#include "test_translation_helpers.h"
#include "website/translation/TranslationStatusTable.h"

class Test_TranslationStatusTable : public QObject
{
    Q_OBJECT

private slots:
    void test_status_table_empty_row_count_before_reload();
    void test_status_table_row_count_after_reload();
    void test_status_table_excludes_translation_pages();
    void test_status_table_column_count_fixed_plus_langs();
    void test_status_table_permalink_col();
    void test_status_table_type_col();
    void test_status_table_lang_col();
    void test_status_table_lang_col_unchecked_when_not_translated();
    void test_status_table_lang_col_checked_for_source_lang();
    void test_status_table_header_data_fixed_columns();
    void test_status_table_header_data_lang_columns();
    void test_status_table_text_col_flags_enabled_not_checkable();
    void test_status_table_lang_col_flags_display_only();
    void test_status_table_reload_reflects_new_translation();
    void test_status_table_invalid_index_returns_empty();
    void test_status_table_progress_col_zero_when_not_translated();
    void test_status_table_progress_col_full_when_source_lang();
    void test_status_table_progress_col_partial();
    void test_status_table_progress_col_header_not_empty();
};

void Test_TranslationStatusTable::test_status_table_empty_row_count_before_reload()
{
    DbFixture f;
    f.repo.create(QStringLiteral("article"), QStringLiteral("/a.html"), QStringLiteral("fr"));

    TranslationStatusTable table(f.repo, f.categoryTable, {QStringLiteral("de")});
    QCOMPARE(table.rowCount(), 0); // reload() not called yet
}

void Test_TranslationStatusTable::test_status_table_row_count_after_reload()
{
    DbFixture f;
    f.repo.create(QStringLiteral("article"), QStringLiteral("/a.html"), QStringLiteral("fr"));
    f.repo.create(QStringLiteral("article"), QStringLiteral("/b.html"), QStringLiteral("fr"));

    TranslationStatusTable table(f.repo, f.categoryTable, {QStringLiteral("de")});
    table.reload();
    QCOMPARE(table.rowCount(), 2);
}

void Test_TranslationStatusTable::test_status_table_excludes_translation_pages()
{
    DbFixture f;
    const int srcId = f.repo.create(QStringLiteral("article"),
                                    QStringLiteral("/a.html"),
                                    QStringLiteral("fr"));
    // Legacy translation page — must be excluded
    f.repo.createTranslation(srcId,
                              QStringLiteral("article"),
                              QStringLiteral("/a-de.html"),
                              QStringLiteral("de"));

    TranslationStatusTable table(f.repo, f.categoryTable, {QStringLiteral("de")});
    table.reload();
    QCOMPARE(table.rowCount(), 1); // only the source page
}

void Test_TranslationStatusTable::test_status_table_column_count_fixed_plus_langs()
{
    DbFixture f;
    const QStringList langs = {QStringLiteral("de"), QStringLiteral("it"), QStringLiteral("es")};
    TranslationStatusTable table(f.repo, f.categoryTable, langs);
    QCOMPARE(table.columnCount(),
             TranslationStatusTable::COL_COUNT_FIXED + langs.size());
}

void Test_TranslationStatusTable::test_status_table_permalink_col()
{
    DbFixture f;
    f.repo.create(QStringLiteral("article"), QStringLiteral("/my-page.html"), QStringLiteral("fr"));

    TranslationStatusTable table(f.repo, f.categoryTable, {QStringLiteral("de")});
    table.reload();

    const QModelIndex idx = table.index(0, TranslationStatusTable::COL_PERMALINK);
    QCOMPARE(table.data(idx).toString(), QStringLiteral("/my-page.html"));
}

void Test_TranslationStatusTable::test_status_table_type_col()
{
    DbFixture f;
    f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("fr"));

    TranslationStatusTable table(f.repo, f.categoryTable, {QStringLiteral("de")});
    table.reload();

    const QModelIndex idx = table.index(0, TranslationStatusTable::COL_TYPE);
    QCOMPARE(table.data(idx).toString(), QStringLiteral("article"));
}

void Test_TranslationStatusTable::test_status_table_lang_col()
{
    DbFixture f;
    f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("fr"));

    TranslationStatusTable table(f.repo, f.categoryTable, {QStringLiteral("de")});
    table.reload();

    const QModelIndex idx = table.index(0, TranslationStatusTable::COL_LANG);
    QCOMPARE(table.data(idx).toString(), QStringLiteral("fr"));
}

void Test_TranslationStatusTable::test_status_table_lang_col_unchecked_when_not_translated()
{
    DbFixture f;
    addPageWithText(f.repo, QStringLiteral("article"),
                    QStringLiteral("/p.html"), QStringLiteral("fr"));

    TranslationStatusTable table(f.repo, f.categoryTable, {QStringLiteral("de")});
    table.reload();

    const int deCol = TranslationStatusTable::COL_COUNT_FIXED; // first lang col
    const QModelIndex idx = table.index(0, deCol);
    QCOMPARE(table.data(idx, Qt::CheckStateRole).toInt(),
             static_cast<int>(Qt::Unchecked));
}

void Test_TranslationStatusTable::test_status_table_lang_col_checked_for_source_lang()
{
    // Source lang == "fr"; adding "fr" to targetLangs means it should show Checked
    // because setAuthorLang("fr") makes isTranslationComplete("fr") always true.
    DbFixture f;
    addPageWithText(f.repo, QStringLiteral("article"),
                    QStringLiteral("/p.html"), QStringLiteral("fr"));

    TranslationStatusTable table(f.repo, f.categoryTable, {QStringLiteral("fr")});
    table.reload();

    const int frCol = TranslationStatusTable::COL_COUNT_FIXED;
    const QModelIndex idx = table.index(0, frCol);
    QCOMPARE(table.data(idx, Qt::CheckStateRole).toInt(),
             static_cast<int>(Qt::Checked));
}

void Test_TranslationStatusTable::test_status_table_header_data_fixed_columns()
{
    DbFixture f;
    TranslationStatusTable table(f.repo, f.categoryTable, {QStringLiteral("de")});

    QVERIFY(!table.headerData(TranslationStatusTable::COL_PERMALINK,
                               Qt::Horizontal).toString().isEmpty());
    QVERIFY(!table.headerData(TranslationStatusTable::COL_TYPE,
                               Qt::Horizontal).toString().isEmpty());
    QVERIFY(!table.headerData(TranslationStatusTable::COL_LANG,
                               Qt::Horizontal).toString().isEmpty());
}

void Test_TranslationStatusTable::test_status_table_header_data_lang_columns()
{
    DbFixture f;
    const QStringList langs = {QStringLiteral("de"), QStringLiteral("it")};
    TranslationStatusTable table(f.repo, f.categoryTable, langs);

    QCOMPARE(table.headerData(TranslationStatusTable::COL_COUNT_FIXED,
                               Qt::Horizontal).toString(),
             QStringLiteral("de"));
    QCOMPARE(table.headerData(TranslationStatusTable::COL_COUNT_FIXED + 1,
                               Qt::Horizontal).toString(),
             QStringLiteral("it"));
}

void Test_TranslationStatusTable::test_status_table_text_col_flags_enabled_not_checkable()
{
    DbFixture f;
    f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("fr"));
    TranslationStatusTable table(f.repo, f.categoryTable, {QStringLiteral("de")});
    table.reload();

    const Qt::ItemFlags flags = table.flags(
        table.index(0, TranslationStatusTable::COL_PERMALINK));
    QVERIFY(flags & Qt::ItemIsEnabled);
    QVERIFY(!(flags & Qt::ItemIsUserCheckable));
}

void Test_TranslationStatusTable::test_status_table_lang_col_flags_display_only()
{
    // Language columns show a checkbox via Qt::CheckStateRole but must not be
    // user-interactive: Qt::ItemIsUserCheckable must be absent so that clicks
    // produce no toggle.
    DbFixture f;
    f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("fr"));
    TranslationStatusTable table(f.repo, f.categoryTable, {QStringLiteral("de")});
    table.reload();

    const Qt::ItemFlags flags = table.flags(
        table.index(0, TranslationStatusTable::COL_COUNT_FIXED));
    QVERIFY(flags & Qt::ItemIsEnabled);
    QVERIFY(!(flags & Qt::ItemIsUserCheckable));
}

void Test_TranslationStatusTable::test_status_table_reload_reflects_new_translation()
{
    DbFixture f;
    const int id = addPageWithText(f.repo, QStringLiteral("article"),
                                   QStringLiteral("/p.html"), QStringLiteral("fr"));

    TranslationStatusTable table(f.repo, f.categoryTable, {QStringLiteral("de")});
    table.reload();

    const int deCol = TranslationStatusTable::COL_COUNT_FIXED;
    QCOMPARE(table.data(table.index(0, deCol), Qt::CheckStateRole).toInt(),
             static_cast<int>(Qt::Unchecked));

    // Store German translation
    addTranslation(f.repo, f.categoryTable, id,
                   QStringLiteral("fr"), QStringLiteral("de"),
                   QStringLiteral("Übersetzter Text"));
    table.reload();

    QCOMPARE(table.data(table.index(0, deCol), Qt::CheckStateRole).toInt(),
             static_cast<int>(Qt::Checked));
}

void Test_TranslationStatusTable::test_status_table_invalid_index_returns_empty()
{
    DbFixture f;
    TranslationStatusTable table(f.repo, f.categoryTable, {QStringLiteral("de")});
    table.reload();

    QVERIFY(!table.data(QModelIndex{}).isValid());
    QVERIFY(!table.data(table.index(99, 0)).isValid());
}

void Test_TranslationStatusTable::test_status_table_progress_col_zero_when_not_translated()
{
    DbFixture f;
    addPageWithText(f.repo, QStringLiteral("article"),
                    QStringLiteral("/p.html"), QStringLiteral("fr"));

    TranslationStatusTable table(f.repo, f.categoryTable, {QStringLiteral("de")});
    table.reload();

    const QModelIndex idx = table.index(0, TranslationStatusTable::COL_PROGRESS);
    QCOMPARE(table.data(idx).toString(), QStringLiteral("0/1"));
}

void Test_TranslationStatusTable::test_status_table_progress_col_full_when_source_lang()
{
    // targetLangs = {"fr"} == source lang → all complete → "1/1"
    DbFixture f;
    addPageWithText(f.repo, QStringLiteral("article"),
                    QStringLiteral("/p.html"), QStringLiteral("fr"));

    TranslationStatusTable table(f.repo, f.categoryTable, {QStringLiteral("fr")});
    table.reload();

    const QModelIndex idx = table.index(0, TranslationStatusTable::COL_PROGRESS);
    QCOMPARE(table.data(idx).toString(), QStringLiteral("1/1"));
}

void Test_TranslationStatusTable::test_status_table_progress_col_partial()
{
    // targetLangs = {"fr", "de"}: "fr" == source → complete; "de" not translated → "1/2"
    DbFixture f;
    const int id = addPageWithText(f.repo, QStringLiteral("article"),
                                   QStringLiteral("/p.html"), QStringLiteral("fr"));

    TranslationStatusTable table(f.repo, f.categoryTable,
                                  {QStringLiteral("fr"), QStringLiteral("de")});
    table.reload();

    const QModelIndex idx = table.index(0, TranslationStatusTable::COL_PROGRESS);
    QCOMPARE(table.data(idx).toString(), QStringLiteral("1/2"));

    // After translating to "de", reload → "2/2"
    addTranslation(f.repo, f.categoryTable, id,
                   QStringLiteral("fr"), QStringLiteral("de"),
                   QStringLiteral("Übersetzter Text"));
    table.reload();

    QCOMPARE(table.data(table.index(0, TranslationStatusTable::COL_PROGRESS)).toString(),
             QStringLiteral("2/2"));
}

void Test_TranslationStatusTable::test_status_table_progress_col_header_not_empty()
{
    DbFixture f;
    TranslationStatusTable table(f.repo, f.categoryTable, {QStringLiteral("de")});
    QVERIFY(!table.headerData(TranslationStatusTable::COL_PROGRESS,
                               Qt::Horizontal).toString().isEmpty());
}

QTEST_MAIN(Test_TranslationStatusTable)
#include "test_translation_status_table.moc"
