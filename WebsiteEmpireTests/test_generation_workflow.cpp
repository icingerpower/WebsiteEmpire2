#include <QtTest>

#include <QDir>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "website/pages/GenPageQueue.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/attributes/CategoryTable.h"

// ---------------------------------------------------------------------------
// Helpers — replicate LauncherGeneration logic without Qt app stack
// ---------------------------------------------------------------------------

namespace {

static const QRegularExpression s_reNonAlnum(QStringLiteral("[^a-z0-9]+"));

/** Creates a minimal aspire-style source DB with a `records` table. */
static void createSourceDb(const QString &path, const QStringList &topics)
{
    static int s_idx = 0;
    const QString conn = QStringLiteral("src_helper_") + QString::number(++s_idx);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery q(db);
        q.exec(QStringLiteral(
            "CREATE TABLE records (id INTEGER PRIMARY KEY, health_condition TEXT)"));
        for (const QString &t : std::as_const(topics)) {
            QSqlQuery ins(db);
            ins.prepare(QStringLiteral(
                "INSERT INTO records (health_condition) VALUES (?)"));
            ins.addBindValue(t);
            ins.exec();
        }
    }
    QSqlDatabase::removeDatabase(conn);
}

/** Slugifies a topic name the same way LauncherGeneration does. */
static QString toSlug(const QString &name)
{
    QString slug = name.toLower();
    slug.replace(s_reNonAlnum, QStringLiteral("-"));
    while (slug.startsWith(QLatin1Char('-'))) { slug.remove(0, 1); }
    while (slug.endsWith(QLatin1Char('-')))   { slug.chop(1); }
    return slug;
}

/** Reads topics from a source DB and builds virtual PageRecords, excluding
 *  those whose permalink already exists in existingPermalinks. */
static QList<PageRecord> buildVirtualPages(const QString       &dbPath,
                                            const QSet<QString> &existingPermalinks,
                                            const QString       &typeId,
                                            const QString       &lang)
{
    static int s_idx = 0;
    const QString conn = QStringLiteral("vp_helper_") + QString::number(++s_idx);
    QStringList names;
    {
        QSqlDatabase srcDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        srcDb.setDatabaseName(dbPath);
        if (srcDb.open()) {
            // Find first non-id column (mirrors LauncherGeneration).
            QString nameColumn;
            QSqlQuery pq(srcDb);
            if (pq.exec(QStringLiteral("PRAGMA table_info(records)"))) {
                while (pq.next()) {
                    const QString col = pq.value(1).toString();
                    if (col != QStringLiteral("id") && nameColumn.isEmpty()) {
                        nameColumn = col;
                    }
                }
            }
            if (!nameColumn.isEmpty()) {
                QSqlQuery q(srcDb);
                q.exec(QStringLiteral("SELECT ") + nameColumn
                       + QStringLiteral(" FROM records ORDER BY id"));
                while (q.next()) {
                    const QString v = q.value(0).toString().trimmed();
                    if (!v.isEmpty()) {
                        names << v;
                    }
                }
            }
        }
    }
    QSqlDatabase::removeDatabase(conn);

    QList<PageRecord> result;
    for (const QString &name : std::as_const(names)) {
        const QString slug = toSlug(name);
        if (slug.isEmpty()) { continue; }
        const QString permalink = QLatin1Char('/') + slug;
        if (existingPermalinks.contains(permalink)) { continue; }

        PageRecord vp;
        vp.id        = 0;
        vp.typeId    = typeId;
        vp.permalink = permalink;
        vp.lang      = lang;
        result.append(vp);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

struct Fixture {
    QTemporaryDir    dir;
    CategoryTable    categoryTable;
    PageDb           db;
    PageRepositoryDb repo;

    Fixture()
        : categoryTable(QDir(dir.path()))
        , db(QDir(dir.path()))
        , repo(db)
    {
        QVERIFY(dir.isValid());
        QDir(dir.path()).mkdir(QStringLiteral("results_db"));
    }

    QString sourceDbPath() const
    {
        return QDir(dir.path()).filePath(
            QStringLiteral("results_db/PageAttributesHealthCondition.db"));
    }

    QSet<QString> existingPermalinks() const
    {
        const QList<PageRecord> all = repo.findAll();
        QSet<QString> set;
        set.reserve(all.size());
        for (const PageRecord &p : std::as_const(all)) {
            set.insert(p.permalink);
        }
        return set;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class Test_GenerationWorkflow : public QObject
{
    Q_OBJECT

private slots:

    // ==== Slug building =====================================================

    void test_genworkflow_slug_simple_lowercase()
    {
        QCOMPARE(toSlug(QStringLiteral("Osteoarthritis")),
                 QStringLiteral("osteoarthritis"));
    }

    void test_genworkflow_slug_replaces_spaces_with_dashes()
    {
        QCOMPARE(toSlug(QStringLiteral("Rheumatoid Arthritis")),
                 QStringLiteral("rheumatoid-arthritis"));
    }

    void test_genworkflow_slug_strips_leading_trailing_dashes()
    {
        QCOMPARE(toSlug(QStringLiteral("-leading trail-")),
                 QStringLiteral("leading-trail"));
    }

    void test_genworkflow_slug_collapses_consecutive_separators()
    {
        QCOMPARE(toSlug(QStringLiteral("Type  2  Diabetes")),
                 QStringLiteral("type-2-diabetes"));
    }

    // ==== Source DB → virtual pages =========================================

    void test_genworkflow_slug_from_aspire_db_topic()
    {
        Fixture f;
        createSourceDb(f.sourceDbPath(), {QStringLiteral("Osteoarthritis")});
        const QList<PageRecord> pages =
            buildVirtualPages(f.sourceDbPath(), f.existingPermalinks(),
                              QStringLiteral("article"), QStringLiteral("en"));
        QCOMPARE(pages.size(), 1);
        QCOMPARE(pages.at(0).permalink, QStringLiteral("/osteoarthritis"));
    }

    void test_genworkflow_virtual_page_has_correct_fields()
    {
        Fixture f;
        createSourceDb(f.sourceDbPath(), {QStringLiteral("Osteoarthritis")});
        const QList<PageRecord> pages =
            buildVirtualPages(f.sourceDbPath(), f.existingPermalinks(),
                              QStringLiteral("article"), QStringLiteral("en"));
        QCOMPARE(pages.size(), 1);
        QCOMPARE(pages.at(0).id,        0);
        QCOMPARE(pages.at(0).typeId,    QStringLiteral("article"));
        QCOMPARE(pages.at(0).permalink, QStringLiteral("/osteoarthritis"));
        QCOMPARE(pages.at(0).lang,      QStringLiteral("en"));
    }

    void test_genworkflow_already_existing_page_excluded_from_virtual_pages()
    {
        Fixture f;
        createSourceDb(f.sourceDbPath(), {QStringLiteral("Osteoarthritis")});
        // Insert the page into pages.db first.
        f.repo.create(QStringLiteral("article"),
                      QStringLiteral("/osteoarthritis"),
                      QStringLiteral("en"));
        // Now the existing-permalinks set includes /osteoarthritis.
        const QList<PageRecord> pages =
            buildVirtualPages(f.sourceDbPath(), f.existingPermalinks(),
                              QStringLiteral("article"), QStringLiteral("en"));
        QCOMPARE(pages.size(), 0);
    }

    void test_genworkflow_two_topics_two_virtual_pages()
    {
        Fixture f;
        createSourceDb(f.sourceDbPath(),
                       {QStringLiteral("Osteoarthritis"),
                        QStringLiteral("Rheumatoid Arthritis")});
        const QList<PageRecord> pages =
            buildVirtualPages(f.sourceDbPath(), f.existingPermalinks(),
                              QStringLiteral("article"), QStringLiteral("en"));
        QCOMPARE(pages.size(), 2);
        QCOMPARE(pages.at(0).permalink, QStringLiteral("/osteoarthritis"));
        QCOMPARE(pages.at(1).permalink, QStringLiteral("/rheumatoid-arthritis"));
    }

    // ==== GenPageQueue with virtual pages ===================================

    void test_genworkflow_queue_has_one_pending_for_single_topic()
    {
        Fixture f;
        createSourceDb(f.sourceDbPath(), {QStringLiteral("Osteoarthritis")});
        const QList<PageRecord> pages =
            buildVirtualPages(f.sourceDbPath(), f.existingPermalinks(),
                              QStringLiteral("article"), QStringLiteral("en"));
        GenPageQueue queue(QStringLiteral("article"), false, pages,
                           f.categoryTable, QString{});
        QVERIFY(queue.hasNext());
        QCOMPARE(queue.pendingCount(), 1);
    }

    void test_genworkflow_advance_decrements_pending_count()
    {
        Fixture f;
        createSourceDb(f.sourceDbPath(),
                       {QStringLiteral("Osteoarthritis"),
                        QStringLiteral("Arthritis")});
        const QList<PageRecord> pages =
            buildVirtualPages(f.sourceDbPath(), f.existingPermalinks(),
                              QStringLiteral("article"), QStringLiteral("en"));
        GenPageQueue queue(QStringLiteral("article"), false, pages,
                           f.categoryTable, QString{});
        QCOMPARE(queue.pendingCount(), 2);
        queue.advance();
        QCOMPARE(queue.pendingCount(), 1);
        queue.advance();
        QCOMPARE(queue.pendingCount(), 0);
        QVERIFY(!queue.hasNext());
    }

    void test_genworkflow_peek_next_returns_correct_page()
    {
        Fixture f;
        createSourceDb(f.sourceDbPath(), {QStringLiteral("Osteoarthritis")});
        const QList<PageRecord> pages =
            buildVirtualPages(f.sourceDbPath(), f.existingPermalinks(),
                              QStringLiteral("article"), QStringLiteral("en"));
        GenPageQueue queue(QStringLiteral("article"), false, pages,
                           f.categoryTable, QString{});
        QCOMPARE(queue.peekNext().permalink, QStringLiteral("/osteoarthritis"));
    }

    // ==== processReply — full pipeline ======================================

    void test_genworkflow_processreply_saves_data_and_stamps_generated_at()
    {
        Fixture f;
        createSourceDb(f.sourceDbPath(), {QStringLiteral("Osteoarthritis")});
        const QList<PageRecord> pages =
            buildVirtualPages(f.sourceDbPath(), f.existingPermalinks(),
                              QStringLiteral("article"), QStringLiteral("en"));
        QCOMPARE(pages.size(), 1);

        GenPageQueue queue(QStringLiteral("article"), false, pages,
                           f.categoryTable, QString{});

        const PageRecord &vp = queue.peekNext();
        queue.advance();

        // Create the real row (mirrors what LauncherGeneration does just before
        // calling processReply).
        const int pageId = f.repo.create(vp.typeId, vp.permalink, vp.lang);
        QVERIFY(pageId > 0);

        const QString jsonReply = QStringLiteral(
            "{\"1_text\": \"About osteoarthritis.\", \"0_categories\": \"\"}");
        const bool ok = queue.processReply(pageId, jsonReply, f.repo);
        QVERIFY(ok);

        // Data saved.
        const QHash<QString, QString> data = f.repo.loadData(pageId);
        QVERIFY(!data.isEmpty());
        QCOMPARE(data.value(QStringLiteral("1_text")),
                 QStringLiteral("About osteoarthritis."));

        // generated_at stamped → page appears in findGeneratedByTypeId.
        const QList<PageRecord> generated =
            f.repo.findGeneratedByTypeId(QStringLiteral("article"));
        QCOMPARE(generated.size(), 1);
        QCOMPARE(generated.at(0).id, pageId);
    }

    void test_genworkflow_processreply_false_for_invalid_json()
    {
        Fixture f;
        createSourceDb(f.sourceDbPath(), {QStringLiteral("Osteoarthritis")});
        const QList<PageRecord> pages =
            buildVirtualPages(f.sourceDbPath(), f.existingPermalinks(),
                              QStringLiteral("article"), QStringLiteral("en"));
        GenPageQueue queue(QStringLiteral("article"), false, pages,
                           f.categoryTable, QString{});

        const int pageId = f.repo.create(QStringLiteral("article"),
                                          QStringLiteral("/osteoarthritis"),
                                          QStringLiteral("en"));
        QVERIFY(!queue.processReply(pageId, QStringLiteral("not json at all"), f.repo));
    }

    void test_genworkflow_processreply_page_not_pending_after_success()
    {
        Fixture f;
        createSourceDb(f.sourceDbPath(), {QStringLiteral("Osteoarthritis")});
        const QList<PageRecord> pages =
            buildVirtualPages(f.sourceDbPath(), f.existingPermalinks(),
                              QStringLiteral("article"), QStringLiteral("en"));
        GenPageQueue queue(QStringLiteral("article"), false, pages,
                           f.categoryTable, QString{});

        const PageRecord &vp = queue.peekNext();
        queue.advance();

        const int pageId = f.repo.create(vp.typeId, vp.permalink, vp.lang);
        const QString jsonReply = QStringLiteral(
            "{\"1_text\": \"Content.\", \"0_categories\": \"\"}");
        QVERIFY(queue.processReply(pageId, jsonReply, f.repo));

        // The page is no longer pending.
        QVERIFY(f.repo.findPendingByTypeId(QStringLiteral("article")).isEmpty());
    }

    // A page that was "generated" (generated_at set) but whose permalink does NOT
    // match any topic in the aspire DB must not be counted as done.
    // countGeneratedMatchingPermalinks() is the correct "done" metric when a DB
    // is linked: it only counts pages whose permalink appears in the expected set.
    void test_genworkflow_article_not_from_db_not_counted_as_done()
    {
        Fixture f;
        // Aspire DB has one topic: Osteoarthritis → expected permalink /osteoarthritis.
        createSourceDb(f.sourceDbPath(), {QStringLiteral("Osteoarthritis")});

        // Add and "generate" an article whose permalink does NOT match any DB topic.
        const int pageId = f.repo.create(QStringLiteral("article"),
                                          QStringLiteral("/health-test"),
                                          QStringLiteral("en"));
        f.repo.setGeneratedAt(pageId, PageRepositoryDb::currentUtc());

        // findGeneratedByTypeId counts it (1 generated article exists).
        QCOMPARE(f.repo.findGeneratedByTypeId(QStringLiteral("article")).size(), 1);

        // Cross-reference: /health-test is not in {/osteoarthritis} → done = 0.
        const QSet<QString> expectedPermalinks = {QStringLiteral("/osteoarthritis")};
        QCOMPARE(f.repo.countGeneratedMatchingPermalinks(
                     QStringLiteral("article"), expectedPermalinks), 0);
    }

    // Companion: a generated article whose permalink DOES match a DB topic is counted.
    void test_genworkflow_article_from_db_counted_as_done()
    {
        Fixture f;
        createSourceDb(f.sourceDbPath(), {QStringLiteral("Osteoarthritis")});

        const int pageId = f.repo.create(QStringLiteral("article"),
                                          QStringLiteral("/osteoarthritis"),
                                          QStringLiteral("en"));
        f.repo.setGeneratedAt(pageId, PageRepositoryDb::currentUtc());

        const QSet<QString> expectedPermalinks = {QStringLiteral("/osteoarthritis")};
        QCOMPARE(f.repo.countGeneratedMatchingPermalinks(
                     QStringLiteral("article"), expectedPermalinks), 1);
    }

    // A manually-written page (page_data present, generated_at NULL) must NOT be
    // counted as done.  The correct "done" metric is findGeneratedByTypeId(), not
    // countByTypeId() − findPendingByTypeId().
    void test_genworkflow_manual_page_with_data_not_counted_as_generated()
    {
        Fixture f;

        const int pageId = f.repo.create(QStringLiteral("article"),
                                          QStringLiteral("/manual-page"),
                                          QStringLiteral("en"));
        // Write page_data manually — no generated_at stamp.
        QHash<QString, QString> data;
        data.insert(QStringLiteral("1_text"), QStringLiteral("Hand-written content."));
        f.repo.saveData(pageId, data);

        // findGeneratedByTypeId must return 0 — the page was not AI-generated.
        QCOMPARE(f.repo.findGeneratedByTypeId(QStringLiteral("article")).size(), 0);

        // Confirm the buggy formula (total − pending) would have returned 1,
        // which is the wrong answer this test guards against.
        const int total   = f.repo.countByTypeId(QStringLiteral("article"));
        const int pending = f.repo.findPendingByTypeId(QStringLiteral("article")).size();
        QCOMPARE(total,   1);
        QCOMPARE(pending, 0); // excluded by page_data check
        QCOMPARE(total - pending, 1); // the old (wrong) done count
    }
};

QTEST_MAIN(Test_GenerationWorkflow)
#include "test_generation_workflow.moc"
