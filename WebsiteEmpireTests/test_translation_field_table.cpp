#include <QtTest>

#include "website/commonblocs/CommonBlocCookies.h"
#include "website/commonblocs/CommonBlocFooter.h"
#include "website/commonblocs/CommonBlocHeader.h"
#include "website/translation/TranslationFieldTable.h"

class Test_TranslationFieldTable : public QObject
{
    Q_OBJECT

private slots:
    void test_field_table_empty_when_no_blocs();
    void test_field_table_empty_when_bloc_has_no_source_texts();
    void test_field_table_row_count_equals_nonempty_field_count();
    void test_field_table_bloc_name_col();
    void test_field_table_field_id_col();
    void test_field_table_source_col();
    void test_field_table_lang_col_empty_when_not_translated();
    void test_field_table_lang_col_shows_translated_text();
    void test_field_table_column_count_fixed_plus_target_langs();
    void test_field_table_header_data_fixed_columns();
    void test_field_table_header_data_lang_columns();
    void test_field_table_flags_enabled_none_checkable();
    void test_field_table_lang_col_flags_editable();
    void test_field_table_set_data_updates_translation();
    void test_field_table_set_data_rejected_for_fixed_col();
    void test_field_table_multiple_blocs_combined();
    void test_field_table_null_bloc_pointer_skipped();
    void test_field_table_invalid_index_returns_empty();
    void test_field_table_progress_col_zero_when_no_translations();
    void test_field_table_progress_col_counts_filled();
    void test_field_table_progress_col_header_not_empty();
    void test_field_table_progress_col_not_editable();
    void test_field_table_set_data_rejected_for_progress_col();
};

void Test_TranslationFieldTable::test_field_table_empty_when_no_blocs()
{
    TranslationFieldTable table({}, {QStringLiteral("de")});
    QCOMPARE(table.rowCount(), 0);
}

void Test_TranslationFieldTable::test_field_table_empty_when_bloc_has_no_source_texts()
{
    // CommonBlocHeader with no title/subtitle set → sourceTexts() returns empty map
    CommonBlocHeader header;
    TranslationFieldTable table({&header}, {QStringLiteral("de")});
    QCOMPARE(table.rowCount(), 0);
}

void Test_TranslationFieldTable::test_field_table_row_count_equals_nonempty_field_count()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));
    header.setSubtitle(QStringLiteral("Tagline"));

    // Both title and subtitle are non-empty → 2 rows
    TranslationFieldTable table({&header}, {QStringLiteral("de")});
    QCOMPARE(table.rowCount(), 2);
}

void Test_TranslationFieldTable::test_field_table_bloc_name_col()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));

    TranslationFieldTable table({&header}, {QStringLiteral("de")});

    const QModelIndex idx = table.index(0, TranslationFieldTable::COL_BLOC_NAME);
    QCOMPARE(table.data(idx).toString(), header.getName());
}

void Test_TranslationFieldTable::test_field_table_field_id_col()
{
    CommonBlocFooter footer;
    footer.setText(QStringLiteral("Copyright"));

    TranslationFieldTable table({&footer}, {QStringLiteral("de")});

    const QModelIndex idx = table.index(0, TranslationFieldTable::COL_FIELD_LABEL);
    // fieldId for the footer text field is CommonBlocFooter::KEY_TEXT
    QCOMPARE(table.data(idx).toString(),
             QLatin1String(CommonBlocFooter::KEY_TEXT));
}

void Test_TranslationFieldTable::test_field_table_source_col()
{
    CommonBlocFooter footer;
    footer.setText(QStringLiteral("Copyright 2025"));

    TranslationFieldTable table({&footer}, {QStringLiteral("de")});

    const QModelIndex idx = table.index(0, TranslationFieldTable::COL_SOURCE);
    QCOMPARE(table.data(idx).toString(), QStringLiteral("Copyright 2025"));
}

void Test_TranslationFieldTable::test_field_table_lang_col_empty_when_not_translated()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));

    TranslationFieldTable table({&header}, {QStringLiteral("de")});

    const int deCol = TranslationFieldTable::COL_COUNT_FIXED;
    // Find the title row
    const int rows = table.rowCount();
    for (int i = 0; i < rows; ++i) {
        if (table.data(table.index(i, TranslationFieldTable::COL_FIELD_LABEL)).toString()
                == QLatin1String(CommonBlocHeader::KEY_TITLE)) {
            QVERIFY(table.data(table.index(i, deCol)).toString().isEmpty());
            return;
        }
    }
    QFAIL("title row not found");
}

void Test_TranslationFieldTable::test_field_table_lang_col_shows_translated_text()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));
    header.setTranslation(QLatin1String(CommonBlocHeader::KEY_TITLE),
                          QStringLiteral("de"),
                          QStringLiteral("Meine Seite"));

    TranslationFieldTable table({&header}, {QStringLiteral("de")});

    const int deCol = TranslationFieldTable::COL_COUNT_FIXED;
    const int rows = table.rowCount();
    for (int i = 0; i < rows; ++i) {
        if (table.data(table.index(i, TranslationFieldTable::COL_FIELD_LABEL)).toString()
                == QLatin1String(CommonBlocHeader::KEY_TITLE)) {
            QCOMPARE(table.data(table.index(i, deCol)).toString(),
                     QStringLiteral("Meine Seite"));
            return;
        }
    }
    QFAIL("title row not found");
}

void Test_TranslationFieldTable::test_field_table_column_count_fixed_plus_target_langs()
{
    const QStringList langs = {QStringLiteral("de"), QStringLiteral("it"), QStringLiteral("es")};
    TranslationFieldTable table({}, langs);
    QCOMPARE(table.columnCount(),
             TranslationFieldTable::COL_COUNT_FIXED + langs.size());
}

void Test_TranslationFieldTable::test_field_table_header_data_fixed_columns()
{
    TranslationFieldTable table({}, {QStringLiteral("de")});

    QVERIFY(!table.headerData(TranslationFieldTable::COL_BLOC_NAME,
                               Qt::Horizontal).toString().isEmpty());
    QVERIFY(!table.headerData(TranslationFieldTable::COL_FIELD_LABEL,
                               Qt::Horizontal).toString().isEmpty());
    QVERIFY(!table.headerData(TranslationFieldTable::COL_SOURCE,
                               Qt::Horizontal).toString().isEmpty());
}

void Test_TranslationFieldTable::test_field_table_header_data_lang_columns()
{
    const QStringList langs = {QStringLiteral("de"), QStringLiteral("it")};
    TranslationFieldTable table({}, langs);

    QCOMPARE(table.headerData(TranslationFieldTable::COL_COUNT_FIXED,
                               Qt::Horizontal).toString(),
             QStringLiteral("de"));
    QCOMPARE(table.headerData(TranslationFieldTable::COL_COUNT_FIXED + 1,
                               Qt::Horizontal).toString(),
             QStringLiteral("it"));
}

void Test_TranslationFieldTable::test_field_table_flags_enabled_none_checkable()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));
    TranslationFieldTable table({&header}, {QStringLiteral("de")});

    for (int col = 0; col < table.columnCount(); ++col) {
        const Qt::ItemFlags flags = table.flags(table.index(0, col));
        QVERIFY(flags & Qt::ItemIsEnabled);
        QVERIFY(!(flags & Qt::ItemIsUserCheckable));
    }
}

void Test_TranslationFieldTable::test_field_table_lang_col_flags_editable()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));
    TranslationFieldTable table({&header}, {QStringLiteral("de")});

    const Qt::ItemFlags flags = table.flags(
        table.index(0, TranslationFieldTable::COL_COUNT_FIXED));
    QVERIFY(flags & Qt::ItemIsEnabled);
    QVERIFY(flags & Qt::ItemIsEditable);
    QVERIFY(!(flags & Qt::ItemIsUserCheckable));
}

void Test_TranslationFieldTable::test_field_table_set_data_updates_translation()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));
    TranslationFieldTable table({&header}, {QStringLiteral("de")});

    // Find the title row
    int titleRow = -1;
    for (int i = 0; i < table.rowCount(); ++i) {
        if (table.data(table.index(i, TranslationFieldTable::COL_FIELD_LABEL)).toString()
                == QLatin1String(CommonBlocHeader::KEY_TITLE)) {
            titleRow = i;
            break;
        }
    }
    QVERIFY(titleRow != -1);

    const int deCol = TranslationFieldTable::COL_COUNT_FIXED;

    // Initially empty
    QVERIFY(table.data(table.index(titleRow, deCol)).toString().isEmpty());

    // Edit the cell
    const bool ok = table.setData(table.index(titleRow, deCol),
                                   QStringLiteral("Meine Seite"),
                                   Qt::EditRole);
    QVERIFY(ok);

    // Model cell updated
    QCOMPARE(table.data(table.index(titleRow, deCol)).toString(),
             QStringLiteral("Meine Seite"));

    // Bloc's in-memory translation also updated
    QCOMPARE(header.translatedText(QLatin1String(CommonBlocHeader::KEY_TITLE),
                                    QStringLiteral("de")),
             QStringLiteral("Meine Seite"));
}

void Test_TranslationFieldTable::test_field_table_set_data_rejected_for_fixed_col()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));
    TranslationFieldTable table({&header}, {QStringLiteral("de")});

    const bool ok = table.setData(table.index(0, TranslationFieldTable::COL_SOURCE),
                                   QStringLiteral("New source"),
                                   Qt::EditRole);
    QVERIFY(!ok);
    // Source text is unchanged
    QCOMPARE(table.data(table.index(0, TranslationFieldTable::COL_SOURCE)).toString(),
             QStringLiteral("My Site"));
}

void Test_TranslationFieldTable::test_field_table_multiple_blocs_combined()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));     // 1 row

    CommonBlocFooter footer;
    footer.setText(QStringLiteral("Copyright"));    // 1 row

    CommonBlocCookies cookies;
    cookies.setMessage(QStringLiteral("We use cookies"));   // 1 row
    cookies.setButtonText(QStringLiteral("Accept"));        // 1 row

    TranslationFieldTable table({&header, &footer, &cookies}, {QStringLiteral("de")});
    QCOMPARE(table.rowCount(), 4);
}

void Test_TranslationFieldTable::test_field_table_null_bloc_pointer_skipped()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));

    // Pass one null in the middle — must not crash, null is silently ignored
    TranslationFieldTable table({nullptr, &header, nullptr}, {QStringLiteral("de")});
    QCOMPARE(table.rowCount(), 1);
}

void Test_TranslationFieldTable::test_field_table_invalid_index_returns_empty()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));
    TranslationFieldTable table({&header}, {QStringLiteral("de")});

    QVERIFY(!table.data(QModelIndex{}).isValid());
    QVERIFY(!table.data(table.index(99, 0)).isValid());
}

void Test_TranslationFieldTable::test_field_table_progress_col_zero_when_no_translations()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));
    TranslationFieldTable table({&header}, {QStringLiteral("de")});

    const QModelIndex idx = table.index(0, TranslationFieldTable::COL_PROGRESS);
    QCOMPARE(table.data(idx).toString(), QStringLiteral("0/1"));
}

void Test_TranslationFieldTable::test_field_table_progress_col_counts_filled()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));
    header.setTranslation(QLatin1String(CommonBlocHeader::KEY_TITLE),
                          QStringLiteral("de"),
                          QStringLiteral("Meine Seite"));

    // "de" translated, "it" not → title row progress is "1/2"
    TranslationFieldTable table({&header}, {QStringLiteral("de"), QStringLiteral("it")});

    const int rows = table.rowCount();
    for (int i = 0; i < rows; ++i) {
        if (table.data(table.index(i, TranslationFieldTable::COL_FIELD_LABEL)).toString()
                == QLatin1String(CommonBlocHeader::KEY_TITLE)) {
            QCOMPARE(table.data(table.index(i, TranslationFieldTable::COL_PROGRESS)).toString(),
                     QStringLiteral("1/2"));
            return;
        }
    }
    QFAIL("title row not found");
}

void Test_TranslationFieldTable::test_field_table_progress_col_header_not_empty()
{
    TranslationFieldTable table({}, {QStringLiteral("de")});
    QVERIFY(!table.headerData(TranslationFieldTable::COL_PROGRESS,
                               Qt::Horizontal).toString().isEmpty());
}

void Test_TranslationFieldTable::test_field_table_progress_col_not_editable()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));
    TranslationFieldTable table({&header}, {QStringLiteral("de")});

    const Qt::ItemFlags flags = table.flags(
        table.index(0, TranslationFieldTable::COL_PROGRESS));
    QVERIFY(flags & Qt::ItemIsEnabled);
    QVERIFY(!(flags & Qt::ItemIsEditable));
}

void Test_TranslationFieldTable::test_field_table_set_data_rejected_for_progress_col()
{
    CommonBlocHeader header;
    header.setTitle(QStringLiteral("My Site"));
    TranslationFieldTable table({&header}, {QStringLiteral("de")});

    const bool ok = table.setData(table.index(0, TranslationFieldTable::COL_PROGRESS),
                                   QStringLiteral("1/1"),
                                   Qt::EditRole);
    QVERIFY(!ok);
}

QTEST_MAIN(Test_TranslationFieldTable)
#include "test_translation_field_table.moc"
