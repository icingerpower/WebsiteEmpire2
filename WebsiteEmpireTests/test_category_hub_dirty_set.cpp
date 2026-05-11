#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "website/pages/CategoryHubDirtySet.h"

class Test_Website_CategoryHub_DirtySet : public QObject
{
    Q_OBJECT

private slots:
    // --- initial state ---
    void test_hub_dirty_set_fresh_dir_is_empty();
    void test_hub_dirty_set_is_empty_true_initially();

    // --- add ---
    void test_hub_dirty_set_add_single_persists_across_reload();
    void test_hub_dirty_set_add_multiple_persists_across_reload();
    void test_hub_dirty_set_add_duplicate_idempotent();
    void test_hub_dirty_set_is_not_empty_after_add();

    // --- addAll ---
    void test_hub_dirty_set_add_all_persists_across_reload();

    // --- remove ---
    void test_hub_dirty_set_remove_persists_across_reload();
    void test_hub_dirty_set_remove_nonexistent_is_noop();

    // --- clear ---
    void test_hub_dirty_set_clear_empties_set();
    void test_hub_dirty_set_clear_persists_across_reload();
    void test_hub_dirty_set_is_empty_after_clear();

    // --- contains ---
    void test_hub_dirty_set_contains_present_id();
    void test_hub_dirty_set_contains_absent_id();

    // --- corrupt file ---
    void test_hub_dirty_set_corrupt_file_loads_empty();

    // --- crash safety ---
    void test_hub_dirty_set_crash_safety_partial_remove();
};

// ---------------------------------------------------------------------------
// initial state
// ---------------------------------------------------------------------------

void Test_Website_CategoryHub_DirtySet::test_hub_dirty_set_fresh_dir_is_empty()
{
    QTemporaryDir d;
    CategoryHubDirtySet s(QDir(d.path()));
    QVERIFY(s.all().isEmpty());
}

void Test_Website_CategoryHub_DirtySet::test_hub_dirty_set_is_empty_true_initially()
{
    QTemporaryDir d;
    CategoryHubDirtySet s(QDir(d.path()));
    QVERIFY(s.isEmpty());
}

// ---------------------------------------------------------------------------
// add
// ---------------------------------------------------------------------------

void Test_Website_CategoryHub_DirtySet::test_hub_dirty_set_add_single_persists_across_reload()
{
    QTemporaryDir d;
    {
        CategoryHubDirtySet s(QDir(d.path()));
        s.add(42);
    }
    CategoryHubDirtySet s2(QDir(d.path()));
    QVERIFY(s2.contains(42));
    QCOMPARE(s2.all().size(), 1);
}

void Test_Website_CategoryHub_DirtySet::test_hub_dirty_set_add_multiple_persists_across_reload()
{
    QTemporaryDir d;
    {
        CategoryHubDirtySet s(QDir(d.path()));
        s.add(1);
        s.add(2);
        s.add(3);
    }
    CategoryHubDirtySet s2(QDir(d.path()));
    QCOMPARE(s2.all(), QSet<int>({1, 2, 3}));
}

void Test_Website_CategoryHub_DirtySet::test_hub_dirty_set_add_duplicate_idempotent()
{
    QTemporaryDir d;
    CategoryHubDirtySet s(QDir(d.path()));
    s.add(7);
    s.add(7);
    s.add(7);
    QCOMPARE(s.all().size(), 1);
    QVERIFY(s.contains(7));
}

void Test_Website_CategoryHub_DirtySet::test_hub_dirty_set_is_not_empty_after_add()
{
    QTemporaryDir d;
    CategoryHubDirtySet s(QDir(d.path()));
    s.add(1);
    QVERIFY(!s.isEmpty());
}

// ---------------------------------------------------------------------------
// addAll
// ---------------------------------------------------------------------------

void Test_Website_CategoryHub_DirtySet::test_hub_dirty_set_add_all_persists_across_reload()
{
    QTemporaryDir d;
    {
        CategoryHubDirtySet s(QDir(d.path()));
        s.addAll({10, 20, 30});
    }
    CategoryHubDirtySet s2(QDir(d.path()));
    QCOMPARE(s2.all(), QSet<int>({10, 20, 30}));
}

// ---------------------------------------------------------------------------
// remove
// ---------------------------------------------------------------------------

void Test_Website_CategoryHub_DirtySet::test_hub_dirty_set_remove_persists_across_reload()
{
    QTemporaryDir d;
    {
        CategoryHubDirtySet s(QDir(d.path()));
        s.add(1);
        s.add(2);
        s.remove(1);
    }
    CategoryHubDirtySet s2(QDir(d.path()));
    QVERIFY(!s2.contains(1));
    QVERIFY(s2.contains(2));
}

void Test_Website_CategoryHub_DirtySet::test_hub_dirty_set_remove_nonexistent_is_noop()
{
    QTemporaryDir d;
    CategoryHubDirtySet s(QDir(d.path()));
    s.add(5);
    s.remove(999); // not in set
    QCOMPARE(s.all().size(), 1);
    QVERIFY(s.contains(5));
}

// ---------------------------------------------------------------------------
// clear
// ---------------------------------------------------------------------------

void Test_Website_CategoryHub_DirtySet::test_hub_dirty_set_clear_empties_set()
{
    QTemporaryDir d;
    CategoryHubDirtySet s(QDir(d.path()));
    s.addAll({1, 2, 3});
    s.clear();
    QVERIFY(s.all().isEmpty());
}

void Test_Website_CategoryHub_DirtySet::test_hub_dirty_set_clear_persists_across_reload()
{
    QTemporaryDir d;
    {
        CategoryHubDirtySet s(QDir(d.path()));
        s.addAll({1, 2, 3});
        s.clear();
    }
    CategoryHubDirtySet s2(QDir(d.path()));
    QVERIFY(s2.all().isEmpty());
}

void Test_Website_CategoryHub_DirtySet::test_hub_dirty_set_is_empty_after_clear()
{
    QTemporaryDir d;
    CategoryHubDirtySet s(QDir(d.path()));
    s.add(1);
    s.clear();
    QVERIFY(s.isEmpty());
}

// ---------------------------------------------------------------------------
// contains
// ---------------------------------------------------------------------------

void Test_Website_CategoryHub_DirtySet::test_hub_dirty_set_contains_present_id()
{
    QTemporaryDir d;
    CategoryHubDirtySet s(QDir(d.path()));
    s.add(99);
    QVERIFY(s.contains(99));
}

void Test_Website_CategoryHub_DirtySet::test_hub_dirty_set_contains_absent_id()
{
    QTemporaryDir d;
    CategoryHubDirtySet s(QDir(d.path()));
    QVERIFY(!s.contains(99));
}

// ---------------------------------------------------------------------------
// corrupt file
// ---------------------------------------------------------------------------

void Test_Website_CategoryHub_DirtySet::test_hub_dirty_set_corrupt_file_loads_empty()
{
    QTemporaryDir d;
    const QString filePath = QDir(d.path()).filePath(
        QLatin1String(CategoryHubDirtySet::FILENAME));
    {
        QFile f(filePath);
        f.open(QIODevice::WriteOnly);
        f.write("NOT VALID JSON {{{");
    }
    CategoryHubDirtySet s(QDir(d.path()));
    QVERIFY(s.all().isEmpty());
}

// ---------------------------------------------------------------------------
// crash safety
// ---------------------------------------------------------------------------

void Test_Website_CategoryHub_DirtySet::test_hub_dirty_set_crash_safety_partial_remove()
{
    // Simulates: two hub IDs are dirty, hub #1 renders and is removed, then a
    // "crash" occurs before hub #2 is removed.  A new CategoryHubDirtySet
    // opened from the same dir must still contain hub #2.
    QTemporaryDir d;
    {
        CategoryHubDirtySet s(QDir(d.path()));
        s.addAll({1, 2});
        s.remove(1); // hub #1 rendered successfully
        // "crash" before remove(2) — object is destroyed without removing 2
    }
    CategoryHubDirtySet s2(QDir(d.path()));
    QVERIFY(!s2.contains(1));
    QVERIFY(s2.contains(2));
}

QTEST_MAIN(Test_Website_CategoryHub_DirtySet)
#include "test_category_hub_dirty_set.moc"
