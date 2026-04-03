#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "website/WebsiteSettingsTable.h"

namespace {

bool writeSettingsCsv(const QDir &dir, const QList<QStringList> &rows)
{
    QFile file(dir.absoluteFilePath(QStringLiteral("settings_global.csv")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&file);
    out << "Id;Value\n";
    for (const auto &row : rows) {
        out << row.join(';') << '\n';
    }
    return true;
}

} // namespace

class Test_Website_Settings : public QObject
{
    Q_OBJECT

private slots:
    void test_settings_column_count();
    void test_settings_row_count();
    void test_settings_column_headers();
    void test_settings_headerdata_vertical_returns_empty();
    void test_settings_headerdata_invalid_role_returns_empty();
    void test_settings_flags_parameter_not_editable();
    void test_settings_flags_value_editable();
    void test_settings_flags_invalid_index();
    void test_settings_data_displayrole_parameter();
    void test_settings_data_displayrole_value_initially_empty();
    void test_settings_data_editrole_parameter();
    void test_settings_data_userrole_returns_id();
    void test_settings_data_invalid_index_returns_empty();
    void test_settings_rowcount_valid_parent_returns_zero();
    void test_settings_columncount_valid_parent_returns_zero();
    void test_settings_setdata_value();
    void test_settings_setdata_parameter_col_returns_false();
    void test_settings_setdata_invalid_role_returns_false();
    void test_settings_setdata_noop_same_value_returns_false();
    void test_settings_setdata_invalid_index_returns_false();
    void test_settings_valueforid_known();
    void test_settings_valueforid_unknown_returns_empty();
    void test_settings_named_getters_initial();
    void test_settings_named_getters_after_setdata();
    void test_settings_save_and_reload();
    void test_settings_reload_order_changed();
    void test_settings_reload_extra_id_ignored();
    void test_settings_reload_missing_id_defaults_to_empty();
    void test_settings_no_csv_all_values_empty();
};

// ---- Column and row counts --------------------------------------------------

void Test_Website_Settings::test_settings_column_count()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    QCOMPARE(table.columnCount(), 2);
}

void Test_Website_Settings::test_settings_row_count()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    // 3 pre-defined settings: website_name, author, base_url
    QCOMPARE(table.rowCount(), 3);
}

// ---- Headers ----------------------------------------------------------------

void Test_Website_Settings::test_settings_column_headers()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    QVERIFY(!table.headerData(WebsiteSettingsTable::COL_PARAMETER, Qt::Horizontal).toString().isEmpty());
    QVERIFY(!table.headerData(WebsiteSettingsTable::COL_VALUE,     Qt::Horizontal).toString().isEmpty());
}

void Test_Website_Settings::test_settings_headerdata_vertical_returns_empty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    QCOMPARE(table.headerData(0, Qt::Vertical), QVariant());
}

void Test_Website_Settings::test_settings_headerdata_invalid_role_returns_empty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    QCOMPARE(table.headerData(WebsiteSettingsTable::COL_PARAMETER, Qt::Horizontal, Qt::UserRole), QVariant());
}

// ---- Flags ------------------------------------------------------------------

void Test_Website_Settings::test_settings_flags_parameter_not_editable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    const Qt::ItemFlags f = table.flags(table.index(0, WebsiteSettingsTable::COL_PARAMETER));
    QVERIFY(!(f & Qt::ItemIsEditable));
}

void Test_Website_Settings::test_settings_flags_value_editable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    const Qt::ItemFlags f = table.flags(table.index(0, WebsiteSettingsTable::COL_VALUE));
    QVERIFY(f & Qt::ItemIsEditable);
}

void Test_Website_Settings::test_settings_flags_invalid_index()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    QCOMPARE(table.flags(QModelIndex()), Qt::NoItemFlags);
}

// ---- data() -----------------------------------------------------------------

void Test_Website_Settings::test_settings_data_displayrole_parameter()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    // Each row's parameter label must be non-empty.
    for (int row = 0; row < table.rowCount(); ++row) {
        const QString label = table.data(table.index(row, WebsiteSettingsTable::COL_PARAMETER)).toString();
        QVERIFY2(!label.isEmpty(), qPrintable(QStringLiteral("Row %1 has empty parameter label").arg(row)));
    }
}

void Test_Website_Settings::test_settings_data_displayrole_value_initially_empty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    for (int row = 0; row < table.rowCount(); ++row) {
        const QString val = table.data(table.index(row, WebsiteSettingsTable::COL_VALUE)).toString();
        QVERIFY2(val.isEmpty(), qPrintable(QStringLiteral("Row %1 should be empty, got: %2").arg(row).arg(val)));
    }
}

void Test_Website_Settings::test_settings_data_editrole_parameter()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    // EditRole on the parameter column returns the label (same as DisplayRole).
    const QString label = table.data(table.index(0, WebsiteSettingsTable::COL_PARAMETER), Qt::EditRole).toString();
    QVERIFY(!label.isEmpty());
}

void Test_Website_Settings::test_settings_data_userrole_returns_id()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    // Verify the known IDs are present via UserRole.
    QStringList ids;
    for (int row = 0; row < table.rowCount(); ++row) {
        ids << table.data(table.index(row, WebsiteSettingsTable::COL_PARAMETER), Qt::UserRole).toString();
    }
    QVERIFY(ids.contains(WebsiteSettingsTable::ID_WEBSITE_NAME));
    QVERIFY(ids.contains(WebsiteSettingsTable::ID_AUTHOR));
    QVERIFY(ids.contains(WebsiteSettingsTable::ID_BASE_URL));
}

void Test_Website_Settings::test_settings_data_invalid_index_returns_empty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    QCOMPARE(table.data(QModelIndex()), QVariant());
    QCOMPARE(table.data(table.index(table.rowCount(), 0)), QVariant());
    QCOMPARE(table.data(table.index(-1, 0)), QVariant());
}

// ---- Parent checks ----------------------------------------------------------

void Test_Website_Settings::test_settings_rowcount_valid_parent_returns_zero()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    QCOMPARE(table.rowCount(table.index(0, 0)), 0);
}

void Test_Website_Settings::test_settings_columncount_valid_parent_returns_zero()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    QCOMPARE(table.columnCount(table.index(0, 0)), 0);
}

// ---- setData() --------------------------------------------------------------

void Test_Website_Settings::test_settings_setdata_value()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));

    const QModelIndex idx = table.index(0, WebsiteSettingsTable::COL_VALUE);
    QVERIFY(table.setData(idx, QStringLiteral("MyWebsite")));
    QCOMPARE(table.data(idx).toString(), QStringLiteral("MyWebsite"));
}

void Test_Website_Settings::test_settings_setdata_parameter_col_returns_false()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));

    const QModelIndex idx = table.index(0, WebsiteSettingsTable::COL_PARAMETER);
    QVERIFY(!table.setData(idx, QStringLiteral("NewLabel")));
}

void Test_Website_Settings::test_settings_setdata_invalid_role_returns_false()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));

    const QModelIndex idx = table.index(0, WebsiteSettingsTable::COL_VALUE);
    QVERIFY(!table.setData(idx, QStringLiteral("x"), Qt::DisplayRole));
}

void Test_Website_Settings::test_settings_setdata_noop_same_value_returns_false()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));

    const QModelIndex idx = table.index(0, WebsiteSettingsTable::COL_VALUE);
    QVERIFY(table.setData(idx, QStringLiteral("hello")));
    // Setting the same value again must return false (no-op).
    QVERIFY(!table.setData(idx, QStringLiteral("hello")));
}

void Test_Website_Settings::test_settings_setdata_invalid_index_returns_false()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    QVERIFY(!table.setData(QModelIndex(), QStringLiteral("x")));
}

// ---- valueForId() -----------------------------------------------------------

void Test_Website_Settings::test_settings_valueforid_known()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));

    // Find row for website_name and set its value.
    for (int row = 0; row < table.rowCount(); ++row) {
        const QString id = table.data(table.index(row, 0), Qt::UserRole).toString();
        if (id == WebsiteSettingsTable::ID_WEBSITE_NAME) {
            table.setData(table.index(row, WebsiteSettingsTable::COL_VALUE), QStringLiteral("TestSite"));
            break;
        }
    }
    QCOMPARE(table.valueForId(WebsiteSettingsTable::ID_WEBSITE_NAME), QStringLiteral("TestSite"));
}

void Test_Website_Settings::test_settings_valueforid_unknown_returns_empty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    QCOMPARE(table.valueForId(QStringLiteral("nonexistent_id")), QString());
}

// ---- Named getters ----------------------------------------------------------

void Test_Website_Settings::test_settings_named_getters_initial()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    QCOMPARE(table.websiteName(), QString());
    QCOMPARE(table.author(),      QString());
    QCOMPARE(table.baseUrl(),     QString());
}

void Test_Website_Settings::test_settings_named_getters_after_setdata()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));

    // Set each named row by finding it via UserRole.
    for (int row = 0; row < table.rowCount(); ++row) {
        const QString id = table.data(table.index(row, 0), Qt::UserRole).toString();
        const QModelIndex valueIdx = table.index(row, WebsiteSettingsTable::COL_VALUE);
        if (id == WebsiteSettingsTable::ID_WEBSITE_NAME) {
            table.setData(valueIdx, QStringLiteral("My Site"));
        } else if (id == WebsiteSettingsTable::ID_AUTHOR) {
            table.setData(valueIdx, QStringLiteral("Alice"));
        } else if (id == WebsiteSettingsTable::ID_BASE_URL) {
            table.setData(valueIdx, QStringLiteral("https://example.com"));
        }
    }
    QCOMPARE(table.websiteName(), QStringLiteral("My Site"));
    QCOMPARE(table.author(),      QStringLiteral("Alice"));
    QCOMPARE(table.baseUrl(),     QStringLiteral("https://example.com"));
}

// ---- Persistence ------------------------------------------------------------

void Test_Website_Settings::test_settings_save_and_reload()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir qdir(dir.path());

    {
        WebsiteSettingsTable table(qdir);
        for (int row = 0; row < table.rowCount(); ++row) {
            const QString id = table.data(table.index(row, 0), Qt::UserRole).toString();
            if (id == WebsiteSettingsTable::ID_WEBSITE_NAME) {
                table.setData(table.index(row, WebsiteSettingsTable::COL_VALUE), QStringLiteral("ReloadSite"));
            }
        }
    }

    WebsiteSettingsTable reloaded(qdir);
    QCOMPARE(reloaded.websiteName(), QStringLiteral("ReloadSite"));
    // Other rows default to empty.
    QCOMPARE(reloaded.author(),  QString());
    QCOMPARE(reloaded.baseUrl(), QString());
}

void Test_Website_Settings::test_settings_reload_order_changed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir qdir(dir.path());

    // Write CSV with rows in reverse order.
    QVERIFY(writeSettingsCsv(qdir, {
        { WebsiteSettingsTable::ID_BASE_URL,     QStringLiteral("https://example.com") },
        { WebsiteSettingsTable::ID_AUTHOR,       QStringLiteral("Bob") },
        { WebsiteSettingsTable::ID_WEBSITE_NAME, QStringLiteral("OrderTest") },
    }));

    WebsiteSettingsTable table(qdir);
    // All values must be found regardless of CSV order.
    QCOMPARE(table.websiteName(), QStringLiteral("OrderTest"));
    QCOMPARE(table.author(),      QStringLiteral("Bob"));
    QCOMPARE(table.baseUrl(),     QStringLiteral("https://example.com"));
}

void Test_Website_Settings::test_settings_reload_extra_id_ignored()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir qdir(dir.path());

    QVERIFY(writeSettingsCsv(qdir, {
        { WebsiteSettingsTable::ID_WEBSITE_NAME, QStringLiteral("KnownSite") },
        { QStringLiteral("unknown_future_param"), QStringLiteral("ignored") },
    }));

    WebsiteSettingsTable table(qdir);
    QCOMPARE(table.rowCount(), 3);  // only the 3 pre-defined rows
    QCOMPARE(table.websiteName(), QStringLiteral("KnownSite"));
}

void Test_Website_Settings::test_settings_reload_missing_id_defaults_to_empty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir qdir(dir.path());

    // Only website_name saved; author and base_url are absent.
    QVERIFY(writeSettingsCsv(qdir, {
        { WebsiteSettingsTable::ID_WEBSITE_NAME, QStringLiteral("PartialSave") },
    }));

    WebsiteSettingsTable table(qdir);
    QCOMPARE(table.websiteName(), QStringLiteral("PartialSave"));
    QCOMPARE(table.author(),      QString());
    QCOMPARE(table.baseUrl(),     QString());
}

void Test_Website_Settings::test_settings_no_csv_all_values_empty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WebsiteSettingsTable table(QDir(dir.path()));
    // No CSV file — all values must default to empty.
    for (int row = 0; row < table.rowCount(); ++row) {
        const QString val = table.data(table.index(row, WebsiteSettingsTable::COL_VALUE)).toString();
        QVERIFY(val.isEmpty());
    }
}

QTEST_MAIN(Test_Website_Settings)
#include "test_website_settings.moc"
