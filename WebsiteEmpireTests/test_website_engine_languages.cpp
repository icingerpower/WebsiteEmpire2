#include <QtTest>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>

#include "website/AbstractEngine.h"
#include "website/EngineLanguages.h"
#include "website/HostTable.h"
#include "CountryLangManager.h"

namespace {

// Writes engine_domains.csv into dir. Each inner list must have exactly 7
// fields: Enabled;LangCode;Language;Theme;Domain;HostId;HostFolder
bool writeEngineCsv(const QDir &dir, const QList<QStringList> &rows)
{
    QFile file(dir.absoluteFilePath(QStringLiteral("engine_domains.csv")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&file);
    out << "Enabled;LangCode;Language;Theme;Domain;HostId;HostFolder\n";
    for (const auto &row : rows) {
        out << row.join(';') << '\n';
    }
    return true;
}

} // namespace

class Test_Website_Engine_Languages : public QObject
{
    Q_OBJECT

private slots:
    void test_identity();
    void test_registered();
    void test_variations_count();
    void test_variations_ids_match_lang_codes();
    void test_variations_names_nonempty();
    void test_variations_uniqueness();
    void test_initial_model_state();
    void test_column_headers();
    void test_flags();
    void test_load_csv_basic();
    void test_setdata_text_columns();
    void test_setdata_checkbox();
    void test_setdata_noop_returns_false();
    void test_save_and_reload();
    void test_data_invalid_index();
    void test_multi_row_csv();
    void test_host_column_empty_hosttable();
    void test_host_column_with_host();
    void test_reload_clears_old_data();
    void test_enabled_false_in_csv();
    void test_load_removes_langs_not_in_default();
    void test_rowcount_equals_langs_times_variations();
    void test_theme_column_never_empty();
};

// ---- Identity ---------------------------------------------------------------

void Test_Website_Engine_Languages::test_identity()
{
    EngineLanguages engine;
    QCOMPARE(engine.getId(), QStringLiteral("EngineLanguages"));
    QVERIFY(!engine.getName().isEmpty());
    // id and name are distinct — id is a machine key, name is human-readable
    QVERIFY(engine.getId() != engine.getName());
    // getId() is stable: two independent instances return the same value
    QCOMPARE(EngineLanguages().getId(), QStringLiteral("EngineLanguages"));
}

// ---- Registry ---------------------------------------------------------------

void Test_Website_Engine_Languages::test_registered()
{
    const auto &all = AbstractEngine::ALL_ENGINES();
    QVERIFY(all.contains(QStringLiteral("EngineLanguages")));
    const AbstractEngine *proto = all.value(QStringLiteral("EngineLanguages"));
    QVERIFY(proto != nullptr);
    QCOMPARE(proto->getId(), QStringLiteral("EngineLanguages"));
}

// ---- Variations: count (getVariations returns all lang codes as source languages)

void Test_Website_Engine_Languages::test_variations_count()
{
    EngineLanguages engine;
    const QStringList codes = CountryLangManager::instance()->defaultLangCodes();
    QVERIFY(!codes.isEmpty());
    QCOMPARE(codes.size(), 30);

    // getVariations() returns every lang code as a source language — one per entry.
    const QList<AbstractEngine::Variation> variations = engine.getVariations();
    QCOMPARE(variations.size(), codes.size());
    QCOMPARE(variations.first().id, codes.first());
}

// ---- Variations: ids are stable non-empty strings ---------------------------

void Test_Website_Engine_Languages::test_variations_ids_match_lang_codes()
{
    EngineLanguages engine;
    const QStringList codes = CountryLangManager::instance()->defaultLangCodes();
    const QList<AbstractEngine::Variation> variations = engine.getVariations();
    // Each variation id must equal the corresponding lang code (stable DB key).
    QCOMPARE(variations.size(), codes.size());
    for (int i = 0; i < variations.size(); ++i) {
        QCOMPARE(variations.at(i).id, codes.at(i));
        QVERIFY2(!variations.at(i).name.isEmpty(),
                 qPrintable(QStringLiteral("Empty name for id: ") + variations.at(i).id));
    }
}

// ---- Variations: names are non-empty ----------------------------------------

void Test_Website_Engine_Languages::test_variations_names_nonempty()
{
    EngineLanguages engine;
    const QList<AbstractEngine::Variation> variations = engine.getVariations();
    for (const auto &v : variations) {
        QVERIFY2(!v.name.isEmpty(), qPrintable(QStringLiteral("Empty name for id: ") + v.id));
    }
}

// ---- Variations: no duplicates ----------------------------------------------

void Test_Website_Engine_Languages::test_variations_uniqueness()
{
    EngineLanguages engine;
    const QList<AbstractEngine::Variation> variations = engine.getVariations();

    QSet<QString> ids, names;
    for (const auto &v : variations) {
        ids.insert(v.id);
        names.insert(v.name);
    }
    // Every id is unique
    QCOMPARE(ids.size(), variations.size());
    // Every resolved language name is unique
    QCOMPARE(names.size(), variations.size());
}

// ---- Model: initial state after init() with no CSV --------------------------

void Test_Website_Engine_Languages::test_initial_model_state()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineLanguages engine;
    engine.init(QDir(dir.path()), hostTable);

    // No CSV → auto-populated: one row per (lang × theme).
    // EngineLanguages has 30 lang codes and 1 theme → 30 rows.
    const int expectedRows = CountryLangManager::instance()->defaultLangCodes().size()
                             * engine.getVariations().size();
    QCOMPARE(engine.rowCount(), expectedRows);
    QCOMPARE(engine.columnCount(), 6);
    // Tree-model contract: a valid parent index returns 0 children
    QCOMPARE(engine.rowCount(engine.index(0, 0)), 0);
    // First row: lang code from CountryLangManager, theme from getVariations()
    const QStringList langCodes = CountryLangManager::instance()->defaultLangCodes();
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_LANG_CODE)).toString(),
             langCodes.first());
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_THEME)).toString(),
             engine.getVariations().first().id);
    // Row 0 is (zh, zh): same target and source language — unchecked by default
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_LANG_CODE), Qt::CheckStateRole).toInt(),
             static_cast<int>(Qt::Unchecked));
    // Row 1 is (zh, es): different target and source — checked by default
    QCOMPARE(engine.data(engine.index(1, AbstractEngine::COL_LANG_CODE), Qt::CheckStateRole).toInt(),
             static_cast<int>(Qt::Checked));
}

// ---- Model: header labels ---------------------------------------------------

void Test_Website_Engine_Languages::test_column_headers()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineLanguages engine;
    engine.init(QDir(dir.path()), hostTable);

    QVERIFY(!engine.headerData(AbstractEngine::COL_LANG_CODE,   Qt::Horizontal).toString().isEmpty());
    QVERIFY(!engine.headerData(AbstractEngine::COL_LANGUAGE,    Qt::Horizontal).toString().isEmpty());
    QVERIFY(!engine.headerData(AbstractEngine::COL_THEME,       Qt::Horizontal).toString().isEmpty());
    QVERIFY(!engine.headerData(AbstractEngine::COL_DOMAIN,      Qt::Horizontal).toString().isEmpty());
    QVERIFY(!engine.headerData(AbstractEngine::COL_HOST,        Qt::Horizontal).toString().isEmpty());
    QVERIFY(!engine.headerData(AbstractEngine::COL_HOST_FOLDER, Qt::Horizontal).toString().isEmpty());
    // Vertical headers return row numbers (1-based)
    QVERIFY(engine.headerData(0, Qt::Vertical).isValid());
}

// ---- Model: item flags ------------------------------------------------------

void Test_Website_Engine_Languages::test_flags()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeEngineCsv(QDir(dir.path()),
        {{"1", "zh", "Chinese", "zh", "example.com", "", "/var/www"}}));
    HostTable hostTable(QDir(dir.path()));
    EngineLanguages engine;
    engine.init(QDir(dir.path()), hostTable);
    QCOMPARE(engine.rowCount(), 900);  // zh/zh preserved + 899 missing pairs added (30×30)

    // COL_LANG_CODE: user-checkable but NOT text-editable (read-only column)
    const Qt::ItemFlags langFlags = engine.flags(engine.index(0, AbstractEngine::COL_LANG_CODE));
    QVERIFY(!(langFlags & Qt::ItemIsEditable));
    QVERIFY(langFlags & Qt::ItemIsUserCheckable);

    // COL_LANGUAGE, COL_THEME: read-only, not checkable
    for (int col = AbstractEngine::COL_LANGUAGE; col <= AbstractEngine::COL_THEME; ++col) {
        const Qt::ItemFlags f = engine.flags(engine.index(0, col));
        QVERIFY(!(f & Qt::ItemIsEditable));
        QVERIFY(!(f & Qt::ItemIsUserCheckable));
    }

    // COL_DOMAIN, COL_HOST, COL_HOST_FOLDER: editable, not checkable
    for (int col = AbstractEngine::COL_DOMAIN; col <= AbstractEngine::COL_HOST_FOLDER; ++col) {
        const Qt::ItemFlags f = engine.flags(engine.index(0, col));
        QVERIFY(f & Qt::ItemIsEditable);
        QVERIFY(!(f & Qt::ItemIsUserCheckable));
    }

    // Invalid index has no flags
    QCOMPARE(engine.flags(QModelIndex()), Qt::NoItemFlags);
}

// ---- Loading: basic CSV round-trip ------------------------------------------

void Test_Website_Engine_Languages::test_load_csv_basic()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeEngineCsv(QDir(dir.path()),
        {{"1", "zh", "Chinese", "zh", "example.com", "", "/var/www"}}));
    HostTable hostTable(QDir(dir.path()));
    EngineLanguages engine;
    engine.init(QDir(dir.path()), hostTable);

    QCOMPARE(engine.rowCount(), 900);  // zh/zh preserved + 899 missing pairs added (30×30)
    QCOMPARE(engine.columnCount(), 6);
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_LANG_CODE)).toString(),   QStringLiteral("zh"));
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_LANGUAGE)).toString(),    QStringLiteral("Chinese"));
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_THEME)).toString(),       QStringLiteral("zh"));
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_DOMAIN)).toString(),      QStringLiteral("example.com"));
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_HOST_FOLDER)).toString(), QStringLiteral("/var/www"));
    // Enabled=1 → Checked
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_LANG_CODE), Qt::CheckStateRole).toInt(),
             static_cast<int>(Qt::Checked));
    // No host ID → empty host display
    QVERIFY(engine.data(engine.index(0, AbstractEngine::COL_HOST)).toString().isEmpty());
    // EditRole returns same text as DisplayRole for text columns
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_DOMAIN), Qt::EditRole).toString(),
             QStringLiteral("example.com"));
}

// ---- setData: text columns --------------------------------------------------

void Test_Website_Engine_Languages::test_setdata_text_columns()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeEngineCsv(QDir(dir.path()),
        {{"1", "zh", "Chinese", "zh", "example.com", "", ""}}));
    HostTable hostTable(QDir(dir.path()));
    EngineLanguages engine;
    engine.init(QDir(dir.path()), hostTable);
    QCOMPARE(engine.rowCount(), 900);  // zh/zh preserved + 899 missing pairs added (30×30)

    QVERIFY(engine.setData(engine.index(0, AbstractEngine::COL_LANG_CODE), QStringLiteral("fr")));
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_LANG_CODE)).toString(), QStringLiteral("fr"));

    QVERIFY(engine.setData(engine.index(0, AbstractEngine::COL_LANGUAGE), QStringLiteral("French")));
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_LANGUAGE)).toString(), QStringLiteral("French"));

    QVERIFY(engine.setData(engine.index(0, AbstractEngine::COL_THEME), QStringLiteral("news")));
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_THEME)).toString(), QStringLiteral("news"));

    QVERIFY(engine.setData(engine.index(0, AbstractEngine::COL_DOMAIN), QStringLiteral("monsite.fr")));
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_DOMAIN)).toString(), QStringLiteral("monsite.fr"));

    QVERIFY(engine.setData(engine.index(0, AbstractEngine::COL_HOST_FOLDER), QStringLiteral("/srv/http")));
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_HOST_FOLDER)).toString(), QStringLiteral("/srv/http"));
}

// ---- setData: checkbox (CheckStateRole) -------------------------------------

void Test_Website_Engine_Languages::test_setdata_checkbox()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeEngineCsv(QDir(dir.path()),
        {{"1", "zh", "Chinese", "zh", "", "", ""}}));
    HostTable hostTable(QDir(dir.path()));
    EngineLanguages engine;
    engine.init(QDir(dir.path()), hostTable);

    const QModelIndex idx = engine.index(0, AbstractEngine::COL_LANG_CODE);
    QCOMPARE(engine.data(idx, Qt::CheckStateRole).toInt(), static_cast<int>(Qt::Checked));

    QVERIFY(engine.setData(idx, Qt::Unchecked, Qt::CheckStateRole));
    QCOMPARE(engine.data(idx, Qt::CheckStateRole).toInt(), static_cast<int>(Qt::Unchecked));

    QVERIFY(engine.setData(idx, Qt::Checked, Qt::CheckStateRole));
    QCOMPARE(engine.data(idx, Qt::CheckStateRole).toInt(), static_cast<int>(Qt::Checked));
}

// ---- setData: no-op when value is unchanged ---------------------------------

void Test_Website_Engine_Languages::test_setdata_noop_returns_false()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeEngineCsv(QDir(dir.path()),
        {{"1", "zh", "Chinese", "zh", "example.com", "", "/srv"}}));
    HostTable hostTable(QDir(dir.path()));
    EngineLanguages engine;
    engine.init(QDir(dir.path()), hostTable);

    QVERIFY(!engine.setData(engine.index(0, AbstractEngine::COL_LANG_CODE),   QStringLiteral("zh")));
    QVERIFY(!engine.setData(engine.index(0, AbstractEngine::COL_LANGUAGE),    QStringLiteral("Chinese")));
    QVERIFY(!engine.setData(engine.index(0, AbstractEngine::COL_THEME),       QStringLiteral("zh")));
    QVERIFY(!engine.setData(engine.index(0, AbstractEngine::COL_DOMAIN),      QStringLiteral("example.com")));
    QVERIFY(!engine.setData(engine.index(0, AbstractEngine::COL_HOST_FOLDER), QStringLiteral("/srv")));
    // Checkbox: same state → false
    QVERIFY(!engine.setData(engine.index(0, AbstractEngine::COL_LANG_CODE), Qt::Checked, Qt::CheckStateRole));
    // Wrong role → false
    QVERIFY(!engine.setData(engine.index(0, AbstractEngine::COL_LANGUAGE), QStringLiteral("x"), Qt::DecorationRole));
}

// ---- Persistence: save + reload ---------------------------------------------

void Test_Website_Engine_Languages::test_save_and_reload()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeEngineCsv(QDir(dir.path()),
        {{"1", "zh", "Chinese", "zh", "example.com", "", ""}}));
    HostTable hostTable(QDir(dir.path()));

    {
        EngineLanguages engine;
        engine.init(QDir(dir.path()), hostTable);
        QCOMPARE(engine.rowCount(), 900);  // zh/zh preserved + 899 missing pairs added (30×30)
        QVERIFY(engine.setData(engine.index(0, AbstractEngine::COL_DOMAIN), QStringLiteral("voyage.cn")));
        QVERIFY(engine.setData(engine.index(0, AbstractEngine::COL_LANG_CODE), Qt::Unchecked, Qt::CheckStateRole));
    }

    // Fresh engine reads the file written by _save()
    EngineLanguages engine2;
    engine2.init(QDir(dir.path()), hostTable);
    QCOMPARE(engine2.rowCount(), 900);
    QCOMPARE(engine2.data(engine2.index(0, AbstractEngine::COL_LANG_CODE)).toString(),  QStringLiteral("zh"));
    QCOMPARE(engine2.data(engine2.index(0, AbstractEngine::COL_LANGUAGE)).toString(),   QStringLiteral("Chinese"));
    QCOMPARE(engine2.data(engine2.index(0, AbstractEngine::COL_THEME)).toString(),      QStringLiteral("zh"));
    QCOMPARE(engine2.data(engine2.index(0, AbstractEngine::COL_DOMAIN)).toString(),     QStringLiteral("voyage.cn"));
    QCOMPARE(engine2.data(engine2.index(0, AbstractEngine::COL_LANG_CODE), Qt::CheckStateRole).toInt(),
             static_cast<int>(Qt::Unchecked));
}

// ---- data(): invalid / out-of-range indices ---------------------------------

void Test_Website_Engine_Languages::test_data_invalid_index()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineLanguages engine;
    engine.init(QDir(dir.path()), hostTable);

    // No CSV → auto-populated (30 langs × 1 theme = 30 rows)
    const int expectedRows = CountryLangManager::instance()->defaultLangCodes().size()
                             * engine.getVariations().size();
    QCOMPARE(engine.rowCount(), expectedRows);
    QVERIFY(!engine.data(QModelIndex()).isValid());
    // Out-of-range rows return invalid data
    QVERIFY(!engine.data(engine.index(expectedRows, 0)).isValid());
    QVERIFY(!engine.data(engine.index(-1, 0)).isValid());

    // Load one row via CSV; reconciliation fills the full matrix — verify out-of-range access
    QVERIFY(writeEngineCsv(QDir(dir.path()), {{"1", "zh", "", "zh", "", "", ""}}));
    engine.init(QDir(dir.path()), hostTable);
    const int rows2 = CountryLangManager::instance()->defaultLangCodes().size()
                      * engine.getVariations().size();
    QCOMPARE(engine.rowCount(), rows2);
    QVERIFY(!engine.data(engine.index(rows2, 0)).isValid());   // row out of range
    QVERIFY(!engine.data(engine.index(-1, 0)).isValid());      // negative row

    // setData on invalid index always returns false
    QVERIFY(!engine.setData(QModelIndex(), QStringLiteral("x")));
}

// ---- Multiple rows ----------------------------------------------------------

void Test_Website_Engine_Languages::test_multi_row_csv()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeEngineCsv(QDir(dir.path()), {
        {"1", "zh", "Chinese", "zh", "zh.example.com", "", "/zh"},
        {"0", "es", "Spanish", "zh", "es.example.com", "", "/es"},
        {"1", "fr", "French",  "zh", "fr.example.com", "", "/fr"},
    }));
    HostTable hostTable(QDir(dir.path()));
    EngineLanguages engine;
    engine.init(QDir(dir.path()), hostTable);

    QCOMPARE(engine.rowCount(), 900);  // 3 preserved rows + 897 missing pairs added (30×30)
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_LANG_CODE)).toString(), QStringLiteral("zh"));
    QCOMPARE(engine.data(engine.index(1, AbstractEngine::COL_LANG_CODE)).toString(), QStringLiteral("es"));
    QCOMPARE(engine.data(engine.index(2, AbstractEngine::COL_LANG_CODE)).toString(), QStringLiteral("fr"));
    // Row 1 is disabled
    QCOMPARE(engine.data(engine.index(1, AbstractEngine::COL_LANG_CODE), Qt::CheckStateRole).toInt(),
             static_cast<int>(Qt::Unchecked));
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_HOST_FOLDER)).toString(), QStringLiteral("/zh"));
    QCOMPARE(engine.data(engine.index(2, AbstractEngine::COL_DOMAIN)).toString(),      QStringLiteral("fr.example.com"));
}

// ---- Host column: no HostTable entries --------------------------------------

void Test_Website_Engine_Languages::test_host_column_empty_hosttable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Row references a non-existent host UUID
    QVERIFY(writeEngineCsv(QDir(dir.path()),
        {{"1", "zh", "Chinese", "zh", "example.com", "some-non-existent-uuid", ""}}));
    HostTable hostTable(QDir(dir.path()));
    EngineLanguages engine;
    engine.init(QDir(dir.path()), hostTable);

    QCOMPARE(engine.rowCount(), 900);  // zh/zh preserved + 899 missing pairs added (30×30)
    // No hosts → host name resolves to empty
    QVERIFY(engine.data(engine.index(0, AbstractEngine::COL_HOST)).toString().isEmpty());
    // availableHostNames is also empty
    QVERIFY(engine.availableHostNames().isEmpty());
}

// ---- Host column: with a real HostTable entry -------------------------------

void Test_Website_Engine_Languages::test_host_column_with_host()
{
    QTemporaryDir hostDir;
    QTemporaryDir engineDir;
    QVERIFY(hostDir.isValid());
    QVERIFY(engineDir.isValid());

    HostTable hostTable(QDir(hostDir.path()));
    const QString hostId = hostTable.addRow();
    QVERIFY(!hostId.isEmpty());
    hostTable.setData(hostTable.index(0, HostTable::COL_NAME), QStringLiteral("MyServer"));

    QVERIFY(writeEngineCsv(QDir(engineDir.path()),
        {{"1", "zh", "Chinese", "zh", "example.com", hostId, "/var/www"}}));
    EngineLanguages engine;
    engine.init(QDir(engineDir.path()), hostTable);

    QCOMPARE(engine.rowCount(), 900);  // zh/zh preserved + 899 missing pairs added (30×30)
    // COL_HOST displays the host name resolved via the stored UUID
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_HOST)).toString(), QStringLiteral("MyServer"));
    QCOMPARE(engine.availableHostNames().size(), 1);
    QCOMPARE(engine.availableHostNames().first(), QStringLiteral("MyServer"));

    // Setting to same name is a no-op
    QVERIFY(!engine.setData(engine.index(0, AbstractEngine::COL_HOST), QStringLiteral("MyServer")));
    // Setting to an unknown name clears the host (no UUID found → empty stored)
    QVERIFY(engine.setData(engine.index(0, AbstractEngine::COL_HOST), QStringLiteral("Unknown")));
    QVERIFY(engine.data(engine.index(0, AbstractEngine::COL_HOST)).toString().isEmpty());
}

// ---- init() re-called: old rows are cleared ---------------------------------

void Test_Website_Engine_Languages::test_reload_clears_old_data()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Write zh/zh with a known domain value
    QVERIFY(writeEngineCsv(QDir(dir.path()), {
        {"1", "zh", "Chinese", "zh", "old.com", "", ""},
    }));
    HostTable hostTable(QDir(dir.path()));
    EngineLanguages engine;
    engine.init(QDir(dir.path()), hostTable);
    QCOMPARE(engine.rowCount(), 900);
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_DOMAIN)).toString(), QStringLiteral("old.com"));

    // Overwrite CSV with updated domain and reload via init()
    QVERIFY(writeEngineCsv(QDir(dir.path()), {{"1", "zh", "Chinese", "zh", "new.com", "", ""}}));
    engine.init(QDir(dir.path()), hostTable);

    QCOMPARE(engine.rowCount(), 900);
    // The domain reflects the new CSV content
    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_DOMAIN)).toString(), QStringLiteral("new.com"));
}

// ---- Enabled=0 loads as unchecked and is persisted --------------------------

void Test_Website_Engine_Languages::test_enabled_false_in_csv()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeEngineCsv(QDir(dir.path()), {
        {"0", "zh", "Chinese", "zh", "", "", ""},
        {"1", "es", "Spanish", "zh", "", "", ""},
    }));
    HostTable hostTable(QDir(dir.path()));
    EngineLanguages engine;
    engine.init(QDir(dir.path()), hostTable);

    QCOMPARE(engine.data(engine.index(0, AbstractEngine::COL_LANG_CODE), Qt::CheckStateRole).toInt(),
             static_cast<int>(Qt::Unchecked));
    QCOMPARE(engine.data(engine.index(1, AbstractEngine::COL_LANG_CODE), Qt::CheckStateRole).toInt(),
             static_cast<int>(Qt::Checked));

    // Enable the first row and verify it survives a reload
    QVERIFY(engine.setData(engine.index(0, AbstractEngine::COL_LANG_CODE), Qt::Checked, Qt::CheckStateRole));

    EngineLanguages engine2;
    engine2.init(QDir(dir.path()), hostTable);
    QCOMPARE(engine2.data(engine2.index(0, AbstractEngine::COL_LANG_CODE), Qt::CheckStateRole).toInt(),
             static_cast<int>(Qt::Checked));
}

// ---- Rows for removed lang codes are dropped on load ------------------------

void Test_Website_Engine_Languages::test_load_removes_langs_not_in_default()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineLanguages engine;

    // Write 30 valid rows + 2 rows for lang codes no longer in defaultLangCodes()
    QList<QStringList> csvRows;
    for (const QString &code : CountryLangManager::instance()->defaultLangCodes()) {
        csvRows << QStringList{QStringLiteral("1"), code, QString(), QStringLiteral("zh"),
                               QString(), QString(), QString()};
    }
    // sv and da were commented out of defaultLangCodes
    csvRows << QStringList{QStringLiteral("1"), QStringLiteral("sv"), QStringLiteral("Swedish"),
                           QStringLiteral("zh"), QString(), QString(), QString()};
    csvRows << QStringList{QStringLiteral("1"), QStringLiteral("da"), QStringLiteral("Danish"),
                           QStringLiteral("zh"), QString(), QString(), QString()};
    QVERIFY(writeEngineCsv(QDir(dir.path()), csvRows));

    engine.init(QDir(dir.path()), hostTable);

    // sv and da must be removed; only the 30 valid lang rows remain
    const int expected = CountryLangManager::instance()->defaultLangCodes().size()
                         * engine.getVariations().size();
    QCOMPARE(engine.rowCount(), expected);

    for (int row = 0; row < engine.rowCount(); ++row) {
        const QString code = engine.data(engine.index(row, AbstractEngine::COL_LANG_CODE)).toString();
        QVERIFY(code != QLatin1String("sv"));
        QVERIFY(code != QLatin1String("da"));
    }
}

// ---- rowCount == defaultLangCodes().size() * getVariations().size() ---------

void Test_Website_Engine_Languages::test_rowcount_equals_langs_times_variations()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineLanguages engine;

    // Write only 5 of the 900 expected (lang × source-lang) pairs — the rest are missing
    QVERIFY(writeEngineCsv(QDir(dir.path()), {
        {QStringLiteral("1"), QStringLiteral("zh"), QStringLiteral("Chinese"),
         QStringLiteral("zh"), QStringLiteral("zh.example.com"), QString(), QString()},
        {QStringLiteral("1"), QStringLiteral("es"), QStringLiteral("Spanish"),
         QStringLiteral("zh"), QStringLiteral("es.example.com"), QString(), QString()},
        {QStringLiteral("1"), QStringLiteral("hi"), QStringLiteral("Hindi"),
         QStringLiteral("zh"), QStringLiteral("hi.example.com"), QString(), QString()},
        {QStringLiteral("1"), QStringLiteral("ar"), QStringLiteral("Arabic"),
         QStringLiteral("zh"), QString(), QString(), QString()},
        {QStringLiteral("1"), QStringLiteral("bn"), QStringLiteral("Bengali"),
         QStringLiteral("zh"), QString(), QString(), QString()},
    }));

    engine.init(QDir(dir.path()), hostTable);

    // Must be completed to the full (target-lang × source-lang) matrix: N × N
    const int expected = CountryLangManager::instance()->defaultLangCodes().size()
                         * (CountryLangManager::instance()->defaultLangCodes().size());
    QCOMPARE(engine.rowCount(), expected);

    // The pre-existing zh/zh row must keep its domain
    int foundZhZh = 0;
    for (int row = 0; row < engine.rowCount(); ++row) {
        const QString lang  = engine.data(engine.index(row, AbstractEngine::COL_LANG_CODE)).toString();
        const QString theme = engine.data(engine.index(row, AbstractEngine::COL_THEME)).toString();
        if (lang == QLatin1String("zh") && theme == QLatin1String("zh")) {
            QCOMPARE(engine.data(engine.index(row, AbstractEngine::COL_DOMAIN)).toString(),
                     QStringLiteral("zh.example.com"));
            ++foundZhZh;
        }
    }
    QCOMPARE(foundZhZh, 1);
}

// ---- Theme column is never empty after init() --------------------------------

void Test_Website_Engine_Languages::test_theme_column_never_empty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineLanguages engine;

    // Write rows with intentionally empty theme values (old format / user-edited CSV)
    QList<QStringList> csvRows;
    for (const QString &code : CountryLangManager::instance()->defaultLangCodes()) {
        csvRows << QStringList{QStringLiteral("1"), code, QString(),
                               QString(), QString(), QString(), QString()};  // theme = ""
    }
    QVERIFY(writeEngineCsv(QDir(dir.path()), csvRows));

    engine.init(QDir(dir.path()), hostTable);

    const int expected = CountryLangManager::instance()->defaultLangCodes().size()
                         * engine.getVariations().size();
    QCOMPARE(engine.rowCount(), expected);

    for (int row = 0; row < engine.rowCount(); ++row) {
        const QString theme = engine.data(engine.index(row, AbstractEngine::COL_THEME)).toString();
        QVERIFY2(!theme.isEmpty(),
                 qPrintable(QStringLiteral("Row %1 has empty theme").arg(row)));
    }
}

QTEST_MAIN(Test_Website_Engine_Languages)
#include "test_website_engine_languages.moc"
