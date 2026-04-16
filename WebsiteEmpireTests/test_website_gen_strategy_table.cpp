#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "GenStrategyTable.h"
#include "aspire/generator/AbstractGenerator.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QString strategyFilePath(const QTemporaryDir &dir)
{
    return QDir(dir.path()).filePath(QStringLiteral("strategies.json"));
}

static bool writeJson(const QTemporaryDir &dir, const QJsonArray &arr)
{
    QFile f(strategyFilePath(dir));
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(QJsonDocument(arr).toJson());
    return true;
}

static QJsonObject makeJsonRow(const QString &id, const QString &name,
                               const QString &pageTypeId   = QStringLiteral("article"),
                               const QString &themeId      = {},
                               const QString &instructions = {},
                               bool           nonSvgImages = false,
                               const QString &primaryAttrId = {})
{
    QJsonObject obj;
    obj[QStringLiteral("id")]            = id;
    obj[QStringLiteral("name")]          = name;
    obj[QStringLiteral("pageTypeId")]    = pageTypeId;
    obj[QStringLiteral("themeId")]       = themeId;
    obj[QStringLiteral("instructions")]  = instructions;
    obj[QStringLiteral("nonSvgImages")]  = nonSvgImages;
    obj[QStringLiteral("primaryAttrId")] = primaryAttrId;
    return obj;
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------
class Test_Website_GenStrategyTable : public QObject
{
    Q_OBJECT

private slots:

    // ==== Construction / initial state ======================================

    void test_strategy_table_empty_on_new_dir()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        QCOMPARE(table.rowCount(), 0);
    }

    void test_strategy_table_loads_empty_when_file_missing()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(!QFile::exists(strategyFilePath(dir)));
        GenStrategyTable table(QDir(dir.path()));
        QCOMPARE(table.rowCount(), 0);
    }

    void test_strategy_table_loads_empty_when_file_not_array()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile f(strategyFilePath(dir));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArrayLiteral("{\"not\": \"an array\"}"));
        f.close();
        GenStrategyTable table(QDir(dir.path()));
        QCOMPARE(table.rowCount(), 0);
    }

    void test_strategy_table_loads_empty_when_file_malformed()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile f(strategyFilePath(dir));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArrayLiteral("not json at all }{["));
        f.close();
        GenStrategyTable table(QDir(dir.path()));
        QCOMPARE(table.rowCount(), 0);
    }

    // ==== addRow ============================================================

    void test_strategy_table_add_row_returns_non_empty_id()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        const QString id = table.addRow(QStringLiteral("Test"), QStringLiteral("article"),
                                        QString{}, QString{}, false);
        QVERIFY(!id.isEmpty());
    }

    void test_strategy_table_add_row_increases_row_count()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        QCOMPARE(table.rowCount(), 0);
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QCOMPARE(table.rowCount(), 1);
        table.addRow(QStringLiteral("B"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QCOMPARE(table.rowCount(), 2);
    }

    void test_strategy_table_add_row_ids_are_unique()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        const QString id1 = table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                                          QString{}, QString{}, false);
        const QString id2 = table.addRow(QStringLiteral("B"), QStringLiteral("article"),
                                          QString{}, QString{}, false);
        QVERIFY(id1 != id2);
    }

    void test_strategy_table_add_row_all_fields_stored()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("MyStrategy"),
                     QStringLiteral("health-condition"),
                     QStringLiteral("theme-1"),
                     QStringLiteral("Custom instructions here"),
                     true,
                     QStringLiteral("PageAttributesHealthCondition"));
        QCOMPARE(table.rowCount(), 1);
        QCOMPARE(table.themeIdForRow(0),            QStringLiteral("theme-1"));
        QCOMPARE(table.customInstructionsForRow(0), QStringLiteral("Custom instructions here"));
        QCOMPARE(table.primaryAttrIdForRow(0),      QStringLiteral("PageAttributesHealthCondition"));
    }

    void test_strategy_table_add_row_primary_attr_id_defaults_to_empty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QCOMPARE(table.primaryAttrIdForRow(0), QString{});
    }

    // ==== rowCount / columnCount ============================================

    void test_strategy_table_column_count_is_seven()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        // Name, PageType, Theme, NonSvgImages, PrimaryAttrId, NDone, NTotal
        QCOMPARE(table.columnCount(), 7);
    }

    void test_strategy_table_column_count_with_valid_parent_returns_zero()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QCOMPARE(table.columnCount(table.index(0, 0)), 0);
    }

    void test_strategy_table_row_count_with_valid_parent_returns_zero()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QCOMPARE(table.rowCount(table.index(0, 0)), 0);
    }

    // ==== Column constant values ============================================

    void test_strategy_table_col_constants_cover_all_columns()
    {
        // All seven column constants must be in [0, columnCount)
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        const int n = table.columnCount();
        QVERIFY(GenStrategyTable::COL_NAME            >= 0 && GenStrategyTable::COL_NAME            < n);
        QVERIFY(GenStrategyTable::COL_PAGE_TYPE        >= 0 && GenStrategyTable::COL_PAGE_TYPE        < n);
        QVERIFY(GenStrategyTable::COL_THEME            >= 0 && GenStrategyTable::COL_THEME            < n);
        QVERIFY(GenStrategyTable::COL_NON_SVG_IMAGES   >= 0 && GenStrategyTable::COL_NON_SVG_IMAGES   < n);
        QVERIFY(GenStrategyTable::COL_PRIMARY_ATTR_ID  >= 0 && GenStrategyTable::COL_PRIMARY_ATTR_ID  < n);
        QVERIFY(GenStrategyTable::COL_N_DONE           >= 0 && GenStrategyTable::COL_N_DONE           < n);
        QVERIFY(GenStrategyTable::COL_N_TOTAL          >= 0 && GenStrategyTable::COL_N_TOTAL          < n);
    }

    void test_strategy_table_col_constants_all_distinct()
    {
        const QList<int> cols = {
            GenStrategyTable::COL_NAME,
            GenStrategyTable::COL_PAGE_TYPE,
            GenStrategyTable::COL_THEME,
            GenStrategyTable::COL_NON_SVG_IMAGES,
            GenStrategyTable::COL_PRIMARY_ATTR_ID,
            GenStrategyTable::COL_N_DONE,
            GenStrategyTable::COL_N_TOTAL,
        };
        const QSet<int> colSet(cols.begin(), cols.end());
        QCOMPARE(colSet.size(), cols.size());
    }

    // ==== idForRow / rowForId ===============================================

    void test_strategy_table_id_for_row_matches_add_row_return()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        const QString id = table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                                         QString{}, QString{}, false);
        QCOMPARE(table.idForRow(0), id);
    }

    void test_strategy_table_id_for_row_negative_returns_empty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        QCOMPARE(table.idForRow(-1), QString{});
    }

    void test_strategy_table_id_for_row_out_of_range_returns_empty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        QCOMPARE(table.idForRow(0), QString{});
    }

    void test_strategy_table_row_for_id_found_at_row_zero()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        const QString id = table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                                         QString{}, QString{}, false);
        QCOMPARE(table.rowForId(id), 0);
    }

    void test_strategy_table_row_for_id_not_found_returns_minus_one()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        QCOMPARE(table.rowForId(QStringLiteral("nonexistent-id")), -1);
    }

    void test_strategy_table_row_for_id_correct_index_with_multiple_rows()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        const QString id2 = table.addRow(QStringLiteral("B"), QStringLiteral("article"),
                                          QString{}, QString{}, false);
        table.addRow(QStringLiteral("C"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QCOMPARE(table.rowForId(id2), 1);
    }

    // ==== themeIdForRow =====================================================

    void test_strategy_table_theme_id_for_row_empty_when_not_set()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QCOMPARE(table.themeIdForRow(0), QString{});
    }

    void test_strategy_table_theme_id_for_row_returns_set_value()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QStringLiteral("my-theme"), QString{}, false);
        QCOMPARE(table.themeIdForRow(0), QStringLiteral("my-theme"));
    }

    void test_strategy_table_theme_id_for_row_out_of_range_returns_empty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        QCOMPARE(table.themeIdForRow(0), QString{});
        QCOMPARE(table.themeIdForRow(-1), QString{});
    }

    // ==== customInstructionsForRow ==========================================

    void test_strategy_table_custom_instructions_empty_when_not_set()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QCOMPARE(table.customInstructionsForRow(0), QString{});
    }

    void test_strategy_table_custom_instructions_returns_set_value()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QStringLiteral("Write SEO content"), false);
        QCOMPARE(table.customInstructionsForRow(0), QStringLiteral("Write SEO content"));
    }

    void test_strategy_table_custom_instructions_out_of_range_returns_empty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        QCOMPARE(table.customInstructionsForRow(5), QString{});
        QCOMPARE(table.customInstructionsForRow(-1), QString{});
    }

    // ==== primaryAttrIdForRow ===============================================

    void test_strategy_table_primary_attr_id_empty_when_not_set()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QCOMPARE(table.primaryAttrIdForRow(0), QString{});
    }

    void test_strategy_table_primary_attr_id_returns_set_value()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false,
                     QStringLiteral("PageAttributesHealthCondition"));
        QCOMPARE(table.primaryAttrIdForRow(0),
                 QStringLiteral("PageAttributesHealthCondition"));
    }

    void test_strategy_table_primary_attr_id_out_of_range_returns_empty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        QCOMPARE(table.primaryAttrIdForRow(0), QString{});
        QCOMPARE(table.primaryAttrIdForRow(-1), QString{});
    }

    // ==== setNDone ==========================================================

    void test_strategy_table_set_n_done_updates_display_value()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        table.setNDone(0, 42);
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_N_DONE)).toInt(), 42);
    }

    void test_strategy_table_set_n_done_emits_data_changed()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QSignalSpy spy(&table, &QAbstractItemModel::dataChanged);
        table.setNDone(0, 7);
        QCOMPARE(spy.count(), 1);
        const QModelIndex idx = spy.at(0).at(0).value<QModelIndex>();
        QCOMPARE(idx.column(), GenStrategyTable::COL_N_DONE);
    }

    void test_strategy_table_set_n_done_noop_when_same_value()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        table.setNDone(0, 5);
        QSignalSpy spy(&table, &QAbstractItemModel::dataChanged);
        table.setNDone(0, 5);
        QCOMPARE(spy.count(), 0);
    }

    void test_strategy_table_set_n_done_out_of_range_noop()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        QSignalSpy spy(&table, &QAbstractItemModel::dataChanged);
        table.setNDone(0, 5); // no rows exist
        QCOMPARE(spy.count(), 0);
    }

    // ==== setNTotal =========================================================

    void test_strategy_table_set_n_total_updates_display_value()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        table.setNTotal(0, 100);
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_N_TOTAL)).toInt(), 100);
    }

    void test_strategy_table_set_n_total_emits_data_changed()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QSignalSpy spy(&table, &QAbstractItemModel::dataChanged);
        table.setNTotal(0, 200);
        QCOMPARE(spy.count(), 1);
        const QModelIndex idx = spy.at(0).at(0).value<QModelIndex>();
        QCOMPARE(idx.column(), GenStrategyTable::COL_N_TOTAL);
    }

    void test_strategy_table_set_n_total_noop_when_same_value()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        table.setNTotal(0, 10);
        QSignalSpy spy(&table, &QAbstractItemModel::dataChanged);
        table.setNTotal(0, 10);
        QCOMPARE(spy.count(), 0);
    }

    void test_strategy_table_set_n_total_out_of_range_noop()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        QSignalSpy spy(&table, &QAbstractItemModel::dataChanged);
        table.setNTotal(5, 100); // no rows exist
        QCOMPARE(spy.count(), 0);
    }

    // ==== data() — DisplayRole ==============================================

    void test_strategy_table_data_col_name()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("My Strategy"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_NAME)).toString(),
                 QStringLiteral("My Strategy"));
    }

    void test_strategy_table_data_col_page_type()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("health-condition"),
                     QString{}, QString{}, false);
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_PAGE_TYPE)).toString(),
                 QStringLiteral("health-condition"));
    }

    void test_strategy_table_data_col_theme_empty_shows_all()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_THEME)).toString(),
                 QStringLiteral("All"));
    }

    void test_strategy_table_data_col_theme_unknown_id_shows_raw()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QStringLiteral("unknown-theme-xyz"), QString{}, false);
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_THEME)).toString(),
                 QStringLiteral("unknown-theme-xyz"));
    }

    void test_strategy_table_data_col_non_svg_images_false_shows_no()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_NON_SVG_IMAGES)).toString(),
                 QStringLiteral("No"));
    }

    void test_strategy_table_data_col_non_svg_images_true_shows_yes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, true);
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_NON_SVG_IMAGES)).toString(),
                 QStringLiteral("Yes"));
    }

    void test_strategy_table_data_col_primary_attr_id_empty_shows_none()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false, QString{});
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_PRIMARY_ATTR_ID)).toString(),
                 QStringLiteral("(None)"));
    }

    void test_strategy_table_data_col_primary_attr_id_known_shows_table_name()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        // GeneratorHealth registers "PageAttributesHealthCondition" as primary
        // with name "Health Conditions" — verify the display resolves it.
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false,
                     QStringLiteral("PageAttributesHealthCondition"));
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_PRIMARY_ATTR_ID)).toString(),
                 QStringLiteral("Health Conditions"));
    }

    void test_strategy_table_data_col_primary_attr_id_unknown_shows_raw()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false,
                     QStringLiteral("PageAttributesUnknownXYZ"));
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_PRIMARY_ATTR_ID)).toString(),
                 QStringLiteral("PageAttributesUnknownXYZ"));
    }

    void test_strategy_table_data_col_n_done_initial_is_zero()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_N_DONE)).toInt(), 0);
    }

    void test_strategy_table_data_col_n_total_initial_is_zero()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_N_TOTAL)).toInt(), 0);
    }

    void test_strategy_table_data_col_n_done_after_set()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        table.setNDone(0, 17);
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_N_DONE)).toInt(), 17);
    }

    void test_strategy_table_data_col_n_total_after_set()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        table.setNTotal(0, 999);
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_N_TOTAL)).toInt(), 999);
    }

    // ==== data() — UserRole =================================================

    void test_strategy_table_data_user_role_returns_row_id()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        const QString id = table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                                         QString{}, QString{}, false);
        QCOMPARE(table.data(table.index(0, 0), Qt::UserRole).toString(), id);
    }

    void test_strategy_table_data_user_role_same_across_all_columns()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        const QString id = table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                                         QString{}, QString{}, false);
        for (int col = 0; col < table.columnCount(); ++col) {
            QCOMPARE(table.data(table.index(0, col), Qt::UserRole).toString(), id);
        }
    }

    // ==== data() — edge cases ===============================================

    void test_strategy_table_data_invalid_index_returns_invalid_variant()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        QVERIFY(!table.data(QModelIndex()).isValid());
    }

    void test_strategy_table_data_out_of_range_row_returns_invalid_variant()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QVERIFY(!table.data(table.index(5, 0)).isValid());
    }

    void test_strategy_table_data_unknown_role_returns_invalid_variant()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QVERIFY(!table.data(table.index(0, 0), Qt::ToolTipRole).isValid());
    }

    // ==== headerData ========================================================

    void test_strategy_table_header_all_horizontal_non_empty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        for (int col = 0; col < table.columnCount(); ++col) {
            QVERIFY(!table.headerData(col, Qt::Horizontal).toString().isEmpty());
        }
    }

    void test_strategy_table_header_all_horizontal_distinct()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        QSet<QString> headers;
        for (int col = 0; col < table.columnCount(); ++col) {
            const QString h = table.headerData(col, Qt::Horizontal).toString();
            QVERIFY2(!headers.contains(h), qPrintable(
                QStringLiteral("Duplicate header at column %1: %2").arg(col).arg(h)));
            headers.insert(h);
        }
    }

    void test_strategy_table_header_col_primary_attr_id_non_empty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        QVERIFY(!table.headerData(GenStrategyTable::COL_PRIMARY_ATTR_ID,
                                   Qt::Horizontal).toString().isEmpty());
    }

    void test_strategy_table_header_vertical_returns_invalid()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        QVERIFY(!table.headerData(0, Qt::Vertical).isValid());
    }

    void test_strategy_table_header_unknown_role_returns_invalid()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        QVERIFY(!table.headerData(0, Qt::Horizontal, Qt::ToolTipRole).isValid());
    }

    // ==== flags =============================================================

    void test_strategy_table_flags_valid_index_not_no_item_flags()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QVERIFY(table.flags(table.index(0, 0)) != Qt::NoItemFlags);
    }

    void test_strategy_table_flags_invalid_index_is_no_item_flags()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        QCOMPARE(table.flags(QModelIndex()), Qt::NoItemFlags);
    }

    // ==== removeRows ========================================================

    void test_strategy_table_remove_row_decreases_count()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        table.addRow(QStringLiteral("B"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QVERIFY(table.removeRows(0, 1));
        QCOMPARE(table.rowCount(), 1);
    }

    void test_strategy_table_remove_row_removes_correct_entry()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("First"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        const QString secondId = table.addRow(QStringLiteral("Second"),
                                               QStringLiteral("article"),
                                               QString{}, QString{}, false);
        table.removeRows(0, 1);
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_NAME)).toString(),
                 QStringLiteral("Second"));
        QCOMPARE(table.idForRow(0), secondId);
    }

    void test_strategy_table_remove_row_returns_false_when_table_empty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        QVERIFY(!table.removeRows(0, 1));
    }

    void test_strategy_table_remove_row_returns_false_for_negative_row()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QVERIFY(!table.removeRows(-1, 1));
    }

    void test_strategy_table_remove_row_returns_false_for_out_of_range()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QVERIFY(!table.removeRows(1, 1));
    }

    void test_strategy_table_remove_row_returns_false_for_zero_count()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GenStrategyTable table(QDir(dir.path()));
        table.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        QVERIFY(!table.removeRows(0, 0));
    }

    // ==== Persistence — roundtrip across instances ==========================

    void test_strategy_table_persists_name()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            GenStrategyTable t(QDir(dir.path()));
            t.addRow(QStringLiteral("My Name"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        }
        GenStrategyTable t2(QDir(dir.path()));
        QCOMPARE(t2.data(t2.index(0, GenStrategyTable::COL_NAME)).toString(),
                 QStringLiteral("My Name"));
    }

    void test_strategy_table_persists_page_type_id()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            GenStrategyTable t(QDir(dir.path()));
            t.addRow(QStringLiteral("A"), QStringLiteral("health-condition"),
                     QString{}, QString{}, false);
        }
        GenStrategyTable t2(QDir(dir.path()));
        QCOMPARE(t2.data(t2.index(0, GenStrategyTable::COL_PAGE_TYPE)).toString(),
                 QStringLiteral("health-condition"));
    }

    void test_strategy_table_persists_theme_id()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            GenStrategyTable t(QDir(dir.path()));
            t.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QStringLiteral("saved-theme"), QString{}, false);
        }
        GenStrategyTable t2(QDir(dir.path()));
        QCOMPARE(t2.themeIdForRow(0), QStringLiteral("saved-theme"));
    }

    void test_strategy_table_persists_custom_instructions()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            GenStrategyTable t(QDir(dir.path()));
            t.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QStringLiteral("SEO rules apply"), false);
        }
        GenStrategyTable t2(QDir(dir.path()));
        QCOMPARE(t2.customInstructionsForRow(0), QStringLiteral("SEO rules apply"));
    }

    void test_strategy_table_persists_primary_attr_id()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            GenStrategyTable t(QDir(dir.path()));
            t.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false,
                     QStringLiteral("PageAttributesHealthCondition"));
        }
        GenStrategyTable t2(QDir(dir.path()));
        QCOMPARE(t2.primaryAttrIdForRow(0),
                 QStringLiteral("PageAttributesHealthCondition"));
    }

    void test_strategy_table_persists_non_svg_images_true()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            GenStrategyTable t(QDir(dir.path()));
            t.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, true);
        }
        GenStrategyTable t2(QDir(dir.path()));
        QCOMPARE(t2.data(t2.index(0, GenStrategyTable::COL_NON_SVG_IMAGES)).toString(),
                 QStringLiteral("Yes"));
    }

    void test_strategy_table_persists_non_svg_images_false()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            GenStrategyTable t(QDir(dir.path()));
            t.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
        }
        GenStrategyTable t2(QDir(dir.path()));
        QCOMPARE(t2.data(t2.index(0, GenStrategyTable::COL_NON_SVG_IMAGES)).toString(),
                 QStringLiteral("No"));
    }

    void test_strategy_table_persists_n_done_after_set()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            GenStrategyTable t(QDir(dir.path()));
            t.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
            t.setNDone(0, 33);
        }
        GenStrategyTable t2(QDir(dir.path()));
        QCOMPARE(t2.data(t2.index(0, GenStrategyTable::COL_N_DONE)).toInt(), 33);
    }

    void test_strategy_table_persists_n_total_after_set()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            GenStrategyTable t(QDir(dir.path()));
            t.addRow(QStringLiteral("A"), QStringLiteral("article"),
                     QString{}, QString{}, false);
            t.setNTotal(0, 500);
        }
        GenStrategyTable t2(QDir(dir.path()));
        QCOMPARE(t2.data(t2.index(0, GenStrategyTable::COL_N_TOTAL)).toInt(), 500);
    }

    void test_strategy_table_persists_row_id_stable_across_instances()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString savedId;
        {
            GenStrategyTable t(QDir(dir.path()));
            savedId = t.addRow(QStringLiteral("A"), QStringLiteral("article"),
                                QString{}, QString{}, false);
        }
        GenStrategyTable t2(QDir(dir.path()));
        QCOMPARE(t2.idForRow(0), savedId);
    }

    void test_strategy_table_persists_multiple_rows_in_insertion_order()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            GenStrategyTable t(QDir(dir.path()));
            t.addRow(QStringLiteral("First"),  QStringLiteral("article"),
                     QString{}, QString{}, false);
            t.addRow(QStringLiteral("Second"), QStringLiteral("article"),
                     QString{}, QString{}, false);
            t.addRow(QStringLiteral("Third"),  QStringLiteral("article"),
                     QString{}, QString{}, false);
        }
        GenStrategyTable t2(QDir(dir.path()));
        QCOMPARE(t2.rowCount(), 3);
        QCOMPARE(t2.data(t2.index(0, GenStrategyTable::COL_NAME)).toString(),
                 QStringLiteral("First"));
        QCOMPARE(t2.data(t2.index(1, GenStrategyTable::COL_NAME)).toString(),
                 QStringLiteral("Second"));
        QCOMPARE(t2.data(t2.index(2, GenStrategyTable::COL_NAME)).toString(),
                 QStringLiteral("Third"));
    }

    void test_strategy_table_persists_after_remove()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            GenStrategyTable t(QDir(dir.path()));
            t.addRow(QStringLiteral("First"),  QStringLiteral("article"),
                     QString{}, QString{}, false);
            t.addRow(QStringLiteral("Second"), QStringLiteral("article"),
                     QString{}, QString{}, false);
            t.removeRows(0, 1);
        }
        GenStrategyTable t2(QDir(dir.path()));
        QCOMPARE(t2.rowCount(), 1);
        QCOMPARE(t2.data(t2.index(0, GenStrategyTable::COL_NAME)).toString(),
                 QStringLiteral("Second"));
    }

    // ==== Migration — loading JSON written by older code ====================

    void test_strategy_table_migration_missing_primary_attr_id_defaults_to_empty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Simulate strategies.json written before primaryAttrId was added.
        QJsonObject row;
        row[QStringLiteral("id")]                 = QStringLiteral("legacy-uuid-1234");
        row[QStringLiteral("name")]               = QStringLiteral("Legacy Row");
        row[QStringLiteral("pageTypeId")]         = QStringLiteral("article");
        row[QStringLiteral("themeId")]            = QString{};
        row[QStringLiteral("customInstructions")] = QString{};
        row[QStringLiteral("nonSvgImages")]       = false;
        row[QStringLiteral("nDone")]              = 5;
        row[QStringLiteral("nTotal")]             = 10;
        // "primaryAttrId" key intentionally absent
        QVERIFY(writeJson(dir, QJsonArray{row}));

        GenStrategyTable table(QDir(dir.path()));
        QCOMPARE(table.rowCount(), 1);
        QCOMPARE(table.primaryAttrIdForRow(0), QString{});
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_NAME)).toString(),
                 QStringLiteral("Legacy Row"));
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_N_DONE)).toInt(), 5);
    }

    void test_strategy_table_migration_row_without_id_gets_generated_id()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonObject row;
        // "id" key absent — should auto-generate a new UUID on load.
        row[QStringLiteral("name")]       = QStringLiteral("No-ID Row");
        row[QStringLiteral("pageTypeId")] = QStringLiteral("article");
        QVERIFY(writeJson(dir, QJsonArray{row}));

        GenStrategyTable table(QDir(dir.path()));
        QCOMPARE(table.rowCount(), 1);
        QVERIFY(!table.idForRow(0).isEmpty());
    }

    void test_strategy_table_migration_skips_non_object_array_elements()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonArray arr;
        arr << QJsonValue(QStringLiteral("not an object"));
        arr << makeJsonRow(QStringLiteral("uuid-a"), QStringLiteral("Valid Row"));
        arr << QJsonValue(42);
        QVERIFY(writeJson(dir, arr));

        GenStrategyTable table(QDir(dir.path()));
        QCOMPARE(table.rowCount(), 1); // only the valid object loaded
        QCOMPARE(table.data(table.index(0, GenStrategyTable::COL_NAME)).toString(),
                 QStringLiteral("Valid Row"));
    }
};

QTEST_MAIN(Test_Website_GenStrategyTable)
#include "test_website_gen_strategy_table.moc"
