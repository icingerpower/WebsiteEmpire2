#include <QtTest>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "website/pages/PageDb.h"

class Test_PageDb : public QObject
{
    Q_OBJECT

private slots:
    // --- schema ---
    void test_pagedb_pages_table_created();
    void test_pagedb_page_data_table_created();
    void test_pagedb_permalink_history_table_created();

    // --- pages columns ---
    void test_pagedb_pages_has_id_column();
    void test_pagedb_pages_has_type_id_column();
    void test_pagedb_pages_has_permalink_column();
    void test_pagedb_pages_has_lang_column();
    void test_pagedb_pages_has_created_at_column();
    void test_pagedb_pages_has_updated_at_column();

    // --- page_data columns ---
    void test_pagedb_page_data_has_page_id_column();
    void test_pagedb_page_data_has_key_column();
    void test_pagedb_page_data_has_value_column();

    // --- permalink_history columns ---
    void test_pagedb_permalink_history_has_page_id_column();
    void test_pagedb_permalink_history_has_old_permalink_column();
    void test_pagedb_permalink_history_has_changed_at_column();

    // --- connection ---
    void test_pagedb_connection_name_non_empty();
    void test_pagedb_two_instances_have_distinct_connections();
    void test_pagedb_database_is_open();

    // --- multiple opens on same dir ---
    void test_pagedb_second_open_same_dir_reuses_schema();
};

// ---------------------------------------------------------------------------

void Test_PageDb::test_pagedb_pages_table_created()
{
    QTemporaryDir dir;
    PageDb db(QDir(dir.path()));
    QSqlQuery q(db.database());
    q.exec(QStringLiteral("SELECT id, type_id, permalink, lang, created_at, updated_at FROM pages LIMIT 0"));
    QVERIFY(!q.lastError().isValid());
}

void Test_PageDb::test_pagedb_page_data_table_created()
{
    QTemporaryDir dir;
    PageDb db(QDir(dir.path()));
    QSqlQuery q(db.database());
    q.exec(QStringLiteral("SELECT page_id, key, value FROM page_data LIMIT 0"));
    QVERIFY(!q.lastError().isValid());
}

void Test_PageDb::test_pagedb_permalink_history_table_created()
{
    QTemporaryDir dir;
    PageDb db(QDir(dir.path()));
    QSqlQuery q(db.database());
    q.exec(QStringLiteral("SELECT id, page_id, old_permalink, changed_at FROM permalink_history LIMIT 0"));
    QVERIFY(!q.lastError().isValid());
}

void Test_PageDb::test_pagedb_pages_has_id_column()
{
    QTemporaryDir dir;
    PageDb db(QDir(dir.path()));
    QSqlQuery q(db.database());
    q.exec(QStringLiteral("SELECT id FROM pages LIMIT 0"));
    QVERIFY(!q.lastError().isValid());
}

void Test_PageDb::test_pagedb_pages_has_type_id_column()
{
    QTemporaryDir dir;
    PageDb db(QDir(dir.path()));
    QSqlQuery q(db.database());
    q.exec(QStringLiteral("SELECT type_id FROM pages LIMIT 0"));
    QVERIFY(!q.lastError().isValid());
}

void Test_PageDb::test_pagedb_pages_has_permalink_column()
{
    QTemporaryDir dir;
    PageDb db(QDir(dir.path()));
    QSqlQuery q(db.database());
    q.exec(QStringLiteral("SELECT permalink FROM pages LIMIT 0"));
    QVERIFY(!q.lastError().isValid());
}

void Test_PageDb::test_pagedb_pages_has_lang_column()
{
    QTemporaryDir dir;
    PageDb db(QDir(dir.path()));
    QSqlQuery q(db.database());
    q.exec(QStringLiteral("SELECT lang FROM pages LIMIT 0"));
    QVERIFY(!q.lastError().isValid());
}

void Test_PageDb::test_pagedb_pages_has_created_at_column()
{
    QTemporaryDir dir;
    PageDb db(QDir(dir.path()));
    QSqlQuery q(db.database());
    q.exec(QStringLiteral("SELECT created_at FROM pages LIMIT 0"));
    QVERIFY(!q.lastError().isValid());
}

void Test_PageDb::test_pagedb_pages_has_updated_at_column()
{
    QTemporaryDir dir;
    PageDb db(QDir(dir.path()));
    QSqlQuery q(db.database());
    q.exec(QStringLiteral("SELECT updated_at FROM pages LIMIT 0"));
    QVERIFY(!q.lastError().isValid());
}

void Test_PageDb::test_pagedb_page_data_has_page_id_column()
{
    QTemporaryDir dir;
    PageDb db(QDir(dir.path()));
    QSqlQuery q(db.database());
    q.exec(QStringLiteral("SELECT page_id FROM page_data LIMIT 0"));
    QVERIFY(!q.lastError().isValid());
}

void Test_PageDb::test_pagedb_page_data_has_key_column()
{
    QTemporaryDir dir;
    PageDb db(QDir(dir.path()));
    QSqlQuery q(db.database());
    q.exec(QStringLiteral("SELECT key FROM page_data LIMIT 0"));
    QVERIFY(!q.lastError().isValid());
}

void Test_PageDb::test_pagedb_page_data_has_value_column()
{
    QTemporaryDir dir;
    PageDb db(QDir(dir.path()));
    QSqlQuery q(db.database());
    q.exec(QStringLiteral("SELECT value FROM page_data LIMIT 0"));
    QVERIFY(!q.lastError().isValid());
}

void Test_PageDb::test_pagedb_permalink_history_has_page_id_column()
{
    QTemporaryDir dir;
    PageDb db(QDir(dir.path()));
    QSqlQuery q(db.database());
    q.exec(QStringLiteral("SELECT page_id FROM permalink_history LIMIT 0"));
    QVERIFY(!q.lastError().isValid());
}

void Test_PageDb::test_pagedb_permalink_history_has_old_permalink_column()
{
    QTemporaryDir dir;
    PageDb db(QDir(dir.path()));
    QSqlQuery q(db.database());
    q.exec(QStringLiteral("SELECT old_permalink FROM permalink_history LIMIT 0"));
    QVERIFY(!q.lastError().isValid());
}

void Test_PageDb::test_pagedb_permalink_history_has_changed_at_column()
{
    QTemporaryDir dir;
    PageDb db(QDir(dir.path()));
    QSqlQuery q(db.database());
    q.exec(QStringLiteral("SELECT changed_at FROM permalink_history LIMIT 0"));
    QVERIFY(!q.lastError().isValid());
}

void Test_PageDb::test_pagedb_connection_name_non_empty()
{
    QTemporaryDir dir;
    PageDb db(QDir(dir.path()));
    QVERIFY(!db.connectionName().isEmpty());
}

void Test_PageDb::test_pagedb_two_instances_have_distinct_connections()
{
    QTemporaryDir d1, d2;
    PageDb db1(QDir(d1.path()));
    PageDb db2(QDir(d2.path()));
    QVERIFY(db1.connectionName() != db2.connectionName());
}

void Test_PageDb::test_pagedb_database_is_open()
{
    QTemporaryDir dir;
    PageDb db(QDir(dir.path()));
    QVERIFY(db.database().isOpen());
}

void Test_PageDb::test_pagedb_second_open_same_dir_reuses_schema()
{
    QTemporaryDir dir;
    {
        PageDb db(QDir(dir.path()));
        QSqlQuery q(db.database());
        q.exec(QStringLiteral(
            "INSERT INTO pages (type_id, permalink, lang, created_at, updated_at)"
            " VALUES ('article', '/test.html', 'en', '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z')"));
    } // db closed and connection removed

    // Re-open: existing data and schema must still be there.
    PageDb db2(QDir(dir.path()));
    QSqlQuery q(db2.database());
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
}

QTEST_MAIN(Test_PageDb)
#include "test_page_db.moc"
