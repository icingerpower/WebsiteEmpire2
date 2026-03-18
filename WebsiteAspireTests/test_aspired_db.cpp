#include <QtTest>
#include <QFile>
#include <QImage>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>

#include "aspire/AspiredDb.h"
#include "aspire/attributes/AbstractPageAttributes.h"
#include "ExceptionWithTitleText.h"

// ---------------------------------------------------------------------------
// Minimal concrete page attributes used in tests — no registry, no Q_OBJECT
// (we only call virtual non-signal methods).
// ---------------------------------------------------------------------------
class SimpleTestAttributes : public AbstractPageAttributes
{
public:
    using AbstractPageAttributes::AbstractPageAttributes;
    QString getId()          const override { return "simple_test"; }
    QString getName()        const override { return "SimpleTest"; }
    QString getDescription() const override { return ""; }
    QSharedPointer<QList<Attribute>> getAttributes() const override
    {
        return QSharedPointer<QList<Attribute>>::create();
    }
};

// Cross-validates that col_a and col_b have the same number of ;-separated parts.
class CrossValidatingAttributes : public AbstractPageAttributes
{
public:
    using AbstractPageAttributes::AbstractPageAttributes;
    QString getId()          const override { return "cross_test"; }
    QString getName()        const override { return "CrossTest"; }
    QString getDescription() const override { return ""; }
    QSharedPointer<QList<Attribute>> getAttributes() const override
    {
        return QSharedPointer<QList<Attribute>>::create();
    }
    QString areAttributesCrossValid(const QHash<QString, QString> &idValues) const override
    {
        const int aCount = idValues.value("col_a").split(';').size();
        const int bCount = idValues.value("col_b").split(';').size();
        if (aCount != bCount)
            return QString("col_a (%1) and col_b (%2) must have the same count").arg(aCount).arg(bCount);
        return QString{};
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace {

// Returns all column names for TABLE in the given SQLite file.
QSet<QString> columnsInTable(const QString &dbPath, const QString &connName, const QString &table)
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
    db.setDatabaseName(dbPath);
    if (!db.open())
        return {};
    QSqlQuery q(db);
    q.exec(QString("PRAGMA table_info(%1)").arg(table));
    QSet<QString> cols;
    while (q.next())
        cols.insert(q.value("name").toString());
    db.close();
    QSqlDatabase::removeDatabase(connName);
    return cols;
}

// Returns every row of TABLE as a list of id→value maps.
QList<QHash<QString, QString>> allRows(const QString &dbPath, const QString &connName, const QString &table)
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
    db.setDatabaseName(dbPath);
    QList<QHash<QString, QString>> rows;
    if (!db.open())
        return rows;
    QSqlQuery q(db);
    q.exec(QString("SELECT * FROM %1").arg(table));
    while (q.next()) {
        QHash<QString, QString> row;
        for (int i = 0; i < q.record().count(); ++i)
            row.insert(q.record().fieldName(i), q.value(i).toString());
        rows.append(row);
    }
    db.close();
    QSqlDatabase::removeDatabase(connName);
    return rows;
}

// Builds a plain attribute (always valid, never empty).
AbstractPageAttributes::Attribute makeAttr(const QString &id, const QString &name,
                                           bool required = false)
{
    return AbstractPageAttributes::Attribute{
        id, name, "", "example", "",
        [required, name](const QString &v) -> QString {
            if (required && v.isEmpty())
                return name + " is required";
            return QString{};
        }
    };
}

// Builds an attribute whose value must be a non-empty integer.
AbstractPageAttributes::Attribute makeIntAttr(const QString &id, const QString &name)
{
    return AbstractPageAttributes::Attribute{
        id, name, "", "42", "",
        [name](const QString &v) -> QString {
            if (v.isEmpty())
                return name + " is required";
            bool ok;
            v.toInt(&ok);
            if (!ok)
                return name + " must be an integer";
            return QString{};
        }
    };
}

// Builds an attribute whose value must be a non-empty double.
AbstractPageAttributes::Attribute makeDoubleAttr(const QString &id, const QString &name)
{
    return AbstractPageAttributes::Attribute{
        id, name, "", "1.5", "",
        [name](const QString &v) -> QString {
            if (v.isEmpty())
                return name + " is required";
            bool ok;
            v.toDouble(&ok);
            if (!ok)
                return name + " must be a number";
            return QString{};
        }
    };
}

// Builds a URL attribute (isUrl = true, required).
AbstractPageAttributes::Attribute makeUrlAttr(const QString &id, const QString &name)
{
    AbstractPageAttributes::Attribute attr;
    attr.id           = id;
    attr.name         = name;
    attr.valueExemple = "https://example.com/page";
    attr.validate     = [name](const QString &v) -> QString {
        if (v.isEmpty()) { return name + " is required"; }
        return QString{};
    };
    attr.isUrl = true;
    return attr;
}

} // namespace

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------
class Test_AspiredDb : public QObject
{
    Q_OBJECT

private slots:
    // --- createTableIdNeed ---
    void test_constructor_creates_db_file();
    void test_createTable_creates_id_primary_key();
    void test_createTable_creates_attribute_columns();
    void test_createTable_is_idempotent_same_attributes();
    void test_createTable_adds_missing_column_when_table_exists();
    void test_createTable_adds_multiple_missing_columns();
    void test_createTable_preserves_existing_rows_after_schema_update();

    // --- record() ---
    void test_record_inserts_valid_row_and_data_is_readable();
    void test_record_inserts_multiple_rows();
    void test_record_throws_on_empty_required_attribute();
    void test_record_throws_on_invalid_integer_format();
    void test_record_throws_on_invalid_double_format();
    void test_record_exception_carries_attribute_name_and_message();
    void test_record_optional_attribute_empty_succeeds();
    void test_record_throws_on_cross_validation_failure();
    void test_record_cross_validation_exception_uses_page_attributes_name();
    void test_record_auto_creates_table_without_prior_createTableIdNeed();
    void test_record_image_single_then_list_of_three();
    void test_record_ignores_duplicate_url();
};

// ---------------------------------------------------------------------------
// 1. Constructor creates the .db file on disk
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_constructor_creates_db_file()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString dbPath = dir.path() + "/myRetriever.db";
    QVERIFY(!QFile::exists(dbPath));

    {
        AspiredDb db(dir.path(), "myRetriever");
        QVERIFY(QFile::exists(dbPath));
    }
    // File persists after object is destroyed
    QVERIFY(QFile::exists(dbPath));

    // A second retriever gets its own file
    const QString dbPath2 = dir.path() + "/otherRetriever.db";
    QVERIFY(!QFile::exists(dbPath2));
    {
        AspiredDb db2(dir.path(), "otherRetriever");
        QVERIFY(QFile::exists(dbPath2));
    }
    QVERIFY(QFile::exists(dbPath2));

    // Both files are independent
    QVERIFY(dbPath != dbPath2);
}

// ---------------------------------------------------------------------------
// 2. createTableIdNeed creates the auto-increment id column
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_createTable_creates_id_primary_key()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    AspiredDb db(dir.path(), "r1");
    db.createTableIdNeed({});   // empty attribute list — just the id column

    const QString dbPath = dir.path() + "/r1.db";
    const auto cols = columnsInTable(dbPath, "conn_test2", AspiredDb::TABLE_NAME);

    QVERIFY(!cols.isEmpty());
    QVERIFY(cols.contains("id"));
    QCOMPARE(cols.size(), 1);   // only the id column
}

// ---------------------------------------------------------------------------
// 3. createTableIdNeed creates one column per attribute
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_createTable_creates_attribute_columns()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<AbstractPageAttributes::Attribute> attrs = {
        makeAttr("title",  "Title",  true),
        makeAttr("author", "Author", true),
        makeAttr("year",   "Year",   false),
    };

    AspiredDb db(dir.path(), "r2");
    db.createTableIdNeed(attrs);

    const QString dbPath = dir.path() + "/r2.db";
    const auto cols = columnsInTable(dbPath, "conn_test3", AspiredDb::TABLE_NAME);

    QVERIFY(cols.contains("id"));
    QVERIFY(cols.contains("title"));
    QVERIFY(cols.contains("author"));
    QVERIFY(cols.contains("year"));
    QCOMPARE(cols.size(), 4);   // id + 3 attributes
}

// ---------------------------------------------------------------------------
// 4. Calling createTableIdNeed twice with the same attributes is idempotent
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_createTable_is_idempotent_same_attributes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<AbstractPageAttributes::Attribute> attrs = {
        makeAttr("col_a", "ColA", true),
        makeAttr("col_b", "ColB", false),
    };

    AspiredDb db(dir.path(), "r3");

    // First call
    db.createTableIdNeed(attrs);

    // Second call — must not throw, must not duplicate columns
    db.createTableIdNeed(attrs);

    const QString dbPath = dir.path() + "/r3.db";
    const auto cols = columnsInTable(dbPath, "conn_test4", AspiredDb::TABLE_NAME);

    QVERIFY(cols.contains("id"));
    QVERIFY(cols.contains("col_a"));
    QVERIFY(cols.contains("col_b"));
    QCOMPARE(cols.size(), 3);
}

// ---------------------------------------------------------------------------
// 5. createTableIdNeed adds a missing column to an existing table
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_createTable_adds_missing_column_when_table_exists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<AbstractPageAttributes::Attribute> initial = {
        makeAttr("col_a", "ColA", true),
        makeAttr("col_b", "ColB", false),
    };
    const QList<AbstractPageAttributes::Attribute> extended = {
        makeAttr("col_a", "ColA", true),
        makeAttr("col_b", "ColB", false),
        makeAttr("col_c", "ColC", false),   // new
    };

    const QString dbPath = dir.path() + "/r4.db";

    {
        AspiredDb db(dir.path(), "r4");
        db.createTableIdNeed(initial);

        auto cols = columnsInTable(dbPath, "conn_test5a", AspiredDb::TABLE_NAME);
        QVERIFY(!cols.contains("col_c"));
        QCOMPARE(cols.size(), 3);   // id + col_a + col_b

        // Now extend the schema
        db.createTableIdNeed(extended);
    }

    const auto cols = columnsInTable(dbPath, "conn_test5b", AspiredDb::TABLE_NAME);
    QVERIFY(cols.contains("id"));
    QVERIFY(cols.contains("col_a"));
    QVERIFY(cols.contains("col_b"));
    QVERIFY(cols.contains("col_c"));
    QCOMPARE(cols.size(), 4);
}

// ---------------------------------------------------------------------------
// 6. createTableIdNeed handles multiple new columns in one call
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_createTable_adds_multiple_missing_columns()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<AbstractPageAttributes::Attribute> initial  = { makeAttr("a", "A") };
    const QList<AbstractPageAttributes::Attribute> extended = {
        makeAttr("a", "A"),
        makeAttr("b", "B"),
        makeAttr("c", "C"),
        makeAttr("d", "D"),
    };

    AspiredDb db(dir.path(), "r5");
    db.createTableIdNeed(initial);
    db.createTableIdNeed(extended);

    const QString dbPath = dir.path() + "/r5.db";
    const auto cols = columnsInTable(dbPath, "conn_test6", AspiredDb::TABLE_NAME);

    QVERIFY(cols.contains("id"));
    QVERIFY(cols.contains("a"));
    QVERIFY(cols.contains("b"));
    QVERIFY(cols.contains("c"));
    QVERIFY(cols.contains("d"));
    QCOMPARE(cols.size(), 5);
}

// ---------------------------------------------------------------------------
// 7. Pre-existing rows are intact after a schema update
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_createTable_preserves_existing_rows_after_schema_update()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<AbstractPageAttributes::Attribute> initial = { makeAttr("col_a", "ColA") };
    const QList<AbstractPageAttributes::Attribute> extended = {
        makeAttr("col_a", "ColA"),
        makeAttr("col_b", "ColB"),
    };
    SimpleTestAttributes pageAttrs;

    AspiredDb db(dir.path(), "r6");
    db.createTableIdNeed(initial);

    // Insert a row with the initial schema
    db.record(initial, {{"col_a", "hello"}}, &pageAttrs);

    // Extend schema
    db.createTableIdNeed(extended);

    // Insert a second row using the extended schema
    db.record(extended, {{"col_a", "world"}, {"col_b", "extra"}}, &pageAttrs);

    const QString dbPath = dir.path() + "/r6.db";
    const auto rows = allRows(dbPath, "conn_test7", AspiredDb::TABLE_NAME);

    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0].value("col_a"), QString("hello"));
    QVERIFY(rows[0].value("col_b").isEmpty());   // NULL → empty string for old row
    QCOMPARE(rows[1].value("col_a"), QString("world"));
    QCOMPARE(rows[1].value("col_b"), QString("extra"));
}

// ---------------------------------------------------------------------------
// 8. record() inserts a row and values are readable from the DB
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_record_inserts_valid_row_and_data_is_readable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<AbstractPageAttributes::Attribute> attrs = {
        makeAttr("product", "Product", true),
        makeAttr("price",   "Price",   true),
        makeAttr("sku",     "SKU",     false),
    };
    const QHash<QString, QString> values = {
        {"product", "Dog Kibble"},
        {"price",   "12.99"},
        {"sku",     "SK-001"},
    };
    SimpleTestAttributes pageAttrs;

    AspiredDb db(dir.path(), "r7");
    db.createTableIdNeed(attrs);
    db.record(attrs, values, &pageAttrs);

    const QString dbPath = dir.path() + "/r7.db";
    const auto rows = allRows(dbPath, "conn_test8", AspiredDb::TABLE_NAME);

    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].value("product"), QString("Dog Kibble"));
    QCOMPARE(rows[0].value("price"),   QString("12.99"));
    QCOMPARE(rows[0].value("sku"),     QString("SK-001"));
    QVERIFY(!rows[0].value("id").isEmpty());   // auto-increment id is assigned
}

// ---------------------------------------------------------------------------
// 9. record() inserts multiple independent rows
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_record_inserts_multiple_rows()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<AbstractPageAttributes::Attribute> attrs = {
        makeAttr("name",  "Name",  true),
        makeAttr("value", "Value", false),
    };
    SimpleTestAttributes pageAttrs;

    AspiredDb db(dir.path(), "r8");
    db.createTableIdNeed(attrs);
    db.record(attrs, {{"name", "alpha"}, {"value", "1"}},   &pageAttrs);
    db.record(attrs, {{"name", "beta"},  {"value", "2"}},   &pageAttrs);
    db.record(attrs, {{"name", "gamma"}, {"value", "3"}},   &pageAttrs);

    const QString dbPath = dir.path() + "/r8.db";
    const auto rows = allRows(dbPath, "conn_test9", AspiredDb::TABLE_NAME);

    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows[0].value("name"),  QString("alpha"));
    QCOMPARE(rows[0].value("value"), QString("1"));
    QCOMPARE(rows[1].value("name"),  QString("beta"));
    QCOMPARE(rows[1].value("value"), QString("2"));
    QCOMPARE(rows[2].value("name"),  QString("gamma"));
    QCOMPARE(rows[2].value("value"), QString("3"));

    // Each row has a unique id
    QVERIFY(rows[0].value("id") != rows[1].value("id"));
    QVERIFY(rows[1].value("id") != rows[2].value("id"));
    QVERIFY(rows[0].value("id") != rows[2].value("id"));
}

// ---------------------------------------------------------------------------
// 10. record() throws ExceptionWithTitleText when a required attribute is empty
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_record_throws_on_empty_required_attribute()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<AbstractPageAttributes::Attribute> attrs = {
        makeAttr("title", "Title", true),
        makeAttr("body",  "Body",  false),
    };
    SimpleTestAttributes pageAttrs;
    AspiredDb db(dir.path(), "r9");
    db.createTableIdNeed(attrs);

    bool threw = false;
    try {
        db.record(attrs, {{"title", ""}, {"body", "some body"}}, &pageAttrs);
    } catch (const ExceptionWithTitleText &e) {
        threw = true;
        QVERIFY(!e.errorTitle().isEmpty());
        QVERIFY(!e.errorText().isEmpty());
        QVERIFY(e.what() != nullptr);
        QVERIFY(QString::fromUtf8(e.what()).contains("Title"));
    }
    QVERIFY(threw);

    // Nothing must have been written to the DB
    const QString dbPath = dir.path() + "/r9.db";
    const auto rows = allRows(dbPath, "conn_test10", AspiredDb::TABLE_NAME);
    QCOMPARE(rows.size(), 0);
}

// ---------------------------------------------------------------------------
// 11. record() throws when an integer attribute receives a non-integer value
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_record_throws_on_invalid_integer_format()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<AbstractPageAttributes::Attribute> attrs = {
        makeIntAttr("weight", "Weight"),
    };
    SimpleTestAttributes pageAttrs;
    AspiredDb db(dir.path(), "r10");
    db.createTableIdNeed(attrs);

    bool threw = false;
    try {
        db.record(attrs, {{"weight", "not_a_number"}}, &pageAttrs);
    } catch (const ExceptionWithTitleText &e) {
        threw = true;
        QVERIFY(!e.errorTitle().isEmpty());
        QVERIFY(!e.errorText().isEmpty());
        QVERIFY(QString(e.what()).contains("Weight"));
    }
    QVERIFY(threw);

    // Also test with a float string (not an integer)
    bool threw2 = false;
    try {
        db.record(attrs, {{"weight", "3.14"}}, &pageAttrs);
    } catch (const ExceptionWithTitleText &e) {
        threw2 = true;
        QVERIFY(!e.errorText().isEmpty());
    }
    QVERIFY(threw2);

    // A valid integer must succeed
    bool noThrow = true;
    try {
        db.record(attrs, {{"weight", "500"}}, &pageAttrs);
    } catch (...) {
        noThrow = false;
    }
    QVERIFY(noThrow);
}

// ---------------------------------------------------------------------------
// 12. record() throws when a double attribute receives a non-numeric value
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_record_throws_on_invalid_double_format()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<AbstractPageAttributes::Attribute> attrs = {
        makeDoubleAttr("price", "Price"),
    };
    SimpleTestAttributes pageAttrs;
    AspiredDb db(dir.path(), "r11");
    db.createTableIdNeed(attrs);

    bool threw = false;
    try {
        db.record(attrs, {{"price", "abc"}}, &pageAttrs);
    } catch (const ExceptionWithTitleText &e) {
        threw = true;
        QVERIFY(!e.errorTitle().isEmpty());
        QVERIFY(!e.errorText().isEmpty());
    }
    QVERIFY(threw);

    // Valid doubles must succeed
    bool noThrow = true;
    try {
        db.record(attrs, {{"price", "9.99"}},  &pageAttrs);
        db.record(attrs, {{"price", "0.0"}},   &pageAttrs);
        db.record(attrs, {{"price", "100"}},   &pageAttrs);
    } catch (...) {
        noThrow = false;
    }
    QVERIFY(noThrow);
}

// ---------------------------------------------------------------------------
// 13. Exception title matches attribute name, text contains the error message
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_record_exception_carries_attribute_name_and_message()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<AbstractPageAttributes::Attribute> attrs = {
        makeAttr("my_field", "MyFieldLabel", true),
    };
    SimpleTestAttributes pageAttrs;
    AspiredDb db(dir.path(), "r12");

    bool threw = false;
    try {
        db.record(attrs, {{"my_field", ""}}, &pageAttrs);
    } catch (const ExceptionWithTitleText &e) {
        threw = true;
        // Title should be the attribute's human-readable name
        QCOMPARE(e.errorTitle(), QString("MyFieldLabel"));
        // Text should be non-empty error description
        QVERIFY(!e.errorText().isEmpty());
        // what() should combine both
        const QString whatStr = QString::fromUtf8(e.what());
        QVERIFY(whatStr.contains("MyFieldLabel"));
    }
    QVERIFY(threw);
}

// ---------------------------------------------------------------------------
// 14. record() succeeds when an optional attribute is absent / empty
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_record_optional_attribute_empty_succeeds()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Optional attribute: validate only if non-empty (is a valid double if given)
    AbstractPageAttributes::Attribute optAttr{
        "notes", "Notes", "", "", "",
        [](const QString &v) -> QString {
            // truly optional — always valid
            Q_UNUSED(v)
            return QString{};
        },
        std::nullopt,
        true   // optional = true
    };
    const QList<AbstractPageAttributes::Attribute> attrs = {
        makeAttr("name", "Name", true),
        optAttr,
    };
    SimpleTestAttributes pageAttrs;
    AspiredDb db(dir.path(), "r13");
    db.createTableIdNeed(attrs);

    bool noThrow = true;
    try {
        // notes is missing from the hash — should succeed
        db.record(attrs, {{"name", "Whiskas"}}, &pageAttrs);
        // notes is explicitly empty — should also succeed
        db.record(attrs, {{"name", "Felix"}, {"notes", ""}}, &pageAttrs);
    } catch (...) {
        noThrow = false;
    }
    QVERIFY(noThrow);

    const QString dbPath = dir.path() + "/r13.db";
    const auto rows = allRows(dbPath, "conn_test14", AspiredDb::TABLE_NAME);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0].value("name"), QString("Whiskas"));
    QCOMPARE(rows[1].value("name"), QString("Felix"));
}

// ---------------------------------------------------------------------------
// 15. record() throws when cross-validation fails (mismatched counts)
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_record_throws_on_cross_validation_failure()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<AbstractPageAttributes::Attribute> attrs = {
        makeAttr("col_a", "ColA"),
        makeAttr("col_b", "ColB"),
    };
    CrossValidatingAttributes pageAttrs;
    AspiredDb db(dir.path(), "r14");
    db.createTableIdNeed(attrs);

    // col_a has 3 parts, col_b has 2 parts → mismatch
    bool threw = false;
    try {
        db.record(attrs, {{"col_a", "x;y;z"}, {"col_b", "a;b"}}, &pageAttrs);
    } catch (const ExceptionWithTitleText &e) {
        threw = true;
        QVERIFY(!e.errorTitle().isEmpty());
        QVERIFY(!e.errorText().isEmpty());
        // Error text must mention the counts
        QVERIFY(e.errorText().contains("3") || e.errorText().contains("2"));
    }
    QVERIFY(threw);

    // Nothing written on failure
    const QString dbPath = dir.path() + "/r14.db";
    const auto rows = allRows(dbPath, "conn_test15a", AspiredDb::TABLE_NAME);
    QCOMPARE(rows.size(), 0);

    // Matching counts must succeed
    bool noThrow = true;
    try {
        db.record(attrs, {{"col_a", "x;y"}, {"col_b", "a;b"}}, &pageAttrs);
    } catch (...) {
        noThrow = false;
    }
    QVERIFY(noThrow);

    const auto rows2 = allRows(dbPath, "conn_test15b", AspiredDb::TABLE_NAME);
    QCOMPARE(rows2.size(), 1);
}

// ---------------------------------------------------------------------------
// 16. Cross-validation exception title == pageAttributes->getName()
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_record_cross_validation_exception_uses_page_attributes_name()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<AbstractPageAttributes::Attribute> attrs = {
        makeAttr("col_a", "ColA"),
        makeAttr("col_b", "ColB"),
    };
    CrossValidatingAttributes pageAttrs;
    AspiredDb db(dir.path(), "r15");

    bool threw = false;
    try {
        db.record(attrs, {{"col_a", "x;y;z"}, {"col_b", "a"}}, &pageAttrs);
    } catch (const ExceptionWithTitleText &e) {
        threw = true;
        // Title must be the page attributes' name
        QCOMPARE(e.errorTitle(), pageAttrs.getName());
        QVERIFY(!e.errorText().isEmpty());
        // what() must combine title and text
        const QString whatStr = QString::fromUtf8(e.what());
        QVERIFY(whatStr.contains(pageAttrs.getName()));
    }
    QVERIFY(threw);
}

// ---------------------------------------------------------------------------
// 17. record() works even if createTableIdNeed was never called explicitly
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_record_auto_creates_table_without_prior_createTableIdNeed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<AbstractPageAttributes::Attribute> attrs = {
        makeAttr("field_x", "FieldX", true),
        makeAttr("field_y", "FieldY", false),
    };
    SimpleTestAttributes pageAttrs;

    AspiredDb db(dir.path(), "r16");
    // Deliberately skip createTableIdNeed

    bool noThrow = true;
    try {
        db.record(attrs, {{"field_x", "hello"}, {"field_y", "world"}}, &pageAttrs);
    } catch (...) {
        noThrow = false;
    }
    QVERIFY(noThrow);

    const QString dbPath = dir.path() + "/r16.db";
    const auto cols = columnsInTable(dbPath, "conn_test17a", AspiredDb::TABLE_NAME);
    QVERIFY(cols.contains("field_x"));
    QVERIFY(cols.contains("field_y"));

    const auto rows = allRows(dbPath, "conn_test17b", AspiredDb::TABLE_NAME);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].value("field_x"), QString("hello"));
    QCOMPARE(rows[0].value("field_y"), QString("world"));
}

// ---------------------------------------------------------------------------
// 18. record() stores one image, then a list of three images
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_record_image_single_then_list_of_three()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Image attribute: requires at least one image.
    AbstractPageAttributes::Attribute imgAttr{
        "photos", "Photos", "", "", "",
        [](const QString &) { return QString{}; },
        std::nullopt,
        false,
        std::nullopt,
        true,
        std::make_optional<std::function<QString(const QList<QSharedPointer<QImage>> &)>>(
            [](const QList<QSharedPointer<QImage>> &images) -> QString {
                if (images.isEmpty()) {
                    return "Photos requires at least one image";
                }
                return QString{};
            })
    };
    const QList<AbstractPageAttributes::Attribute> attrs = { imgAttr };
    SimpleTestAttributes pageAttrs;

    AspiredDb db(dir.path(), "r17");

    // --- First record: one image ---
    auto img1 = QSharedPointer<QImage>::create(4, 4, QImage::Format_RGB32);
    img1->fill(Qt::red);

    db.record(attrs, {}, &pageAttrs, {{"photos", {img1}}});

    // --- Second record: three images ---
    auto img2 = QSharedPointer<QImage>::create(4, 4, QImage::Format_RGB32);
    img2->fill(Qt::green);
    auto img3 = QSharedPointer<QImage>::create(4, 4, QImage::Format_RGB32);
    img3->fill(Qt::blue);

    db.record(attrs, {}, &pageAttrs, {{"photos", {img1, img2, img3}}});

    // Both rows must be present
    const QString dbPath = dir.path() + "/r17.db";
    const auto rows = allRows(dbPath, "conn_test18", AspiredDb::TABLE_NAME);
    QCOMPARE(rows.size(), 2);

    // Both blobs must be non-empty
    QVERIFY(!rows[0].value("photos").isEmpty());
    QVERIFY(!rows[1].value("photos").isEmpty());

    // The 3-image blob must be strictly larger than the 1-image blob
    QVERIFY(rows[1].value("photos").size() > rows[0].value("photos").size());
}

// ---------------------------------------------------------------------------
// 19. record() silently ignores a second insert with the same URL
// ---------------------------------------------------------------------------
void Test_AspiredDb::test_record_ignores_duplicate_url()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<AbstractPageAttributes::Attribute> attrs = {
        makeUrlAttr("url",  "URL"),
        makeAttr("title", "Title", true),
    };
    SimpleTestAttributes pageAttrs;
    AspiredDb db(dir.path(), "r18");

    // First insert — must succeed
    db.record(attrs, {{"url", "https://example.com/p/1"}, {"title", "Page One"}}, &pageAttrs);

    // Second insert with the same URL — must be silently ignored, no exception
    bool noThrow = true;
    try {
        db.record(attrs, {{"url", "https://example.com/p/1"}, {"title", "Page One duplicate"}}, &pageAttrs);
    } catch (...) {
        noThrow = false;
    }
    QVERIFY(noThrow);

    // Still only one row
    const QString dbPath = dir.path() + "/r18.db";
    const auto rows = allRows(dbPath, "conn_test19a", AspiredDb::TABLE_NAME);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].value("title"), QString("Page One"));

    // A different URL must still be accepted
    db.record(attrs, {{"url", "https://example.com/p/2"}, {"title", "Page Two"}}, &pageAttrs);
    const auto rows2 = allRows(dbPath, "conn_test19b", AspiredDb::TABLE_NAME);
    QCOMPARE(rows2.size(), 2);
}

QTEST_MAIN(Test_AspiredDb)
#include "test_aspired_db.moc"
