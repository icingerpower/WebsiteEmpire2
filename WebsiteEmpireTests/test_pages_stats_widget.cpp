#include <QtTest>

#include <QCheckBox>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTableView>
#include <QTemporaryDir>

#include "website/pages/widgets/PagesStatsWidget.h"

class Test_Website_PagesStatsWidget : public QObject
{
    Q_OBJECT

private slots:
    // Regression: the original single-pass query joined raw displays_clicks
    // rows onto raw page_session rows before grouping, so Displays became
    // displays × sessions (e.g. 177 × 91 = 16107 for healybio's /index.html).
    void test_statswidget_displays_not_multiplied_by_sessions();
    void test_statswidget_clicks_counted_once();
    void test_statswidget_avg_time_is_plain_session_average();
    void test_statswidget_page_without_sessions_shows_zero_avgs();
    void test_statswidget_bot_rows_excluded_by_default();
    void test_statswidget_unchecking_filter_shows_bot_rows();

private:
    // Adds nBotDisplays bot-flagged display rows for '/page' to an existing
    // stats.db (the widget must have migrated it, or writeStatsDb ran first
    // and the caller migrates by constructing a widget).
    void addBotDisplays(const QDir &dir, int nBotDisplays);

    // Writes a stats.db in dir with one page: nDisplays display rows
    // (nClicked of them clicked) and the given session times.
    void writeStatsDb(const QDir &dir, int nDisplays, int nClicked,
                      const QList<int> &sessionTimes);

    // Returns the value at (row 0, column) of the widget's table model.
    QVariant cellValue(PagesStatsWidget &widget, int column);
};

void Test_Website_PagesStatsWidget::writeStatsDb(const QDir &dir, int nDisplays,
                                                 int nClicked,
                                                 const QList<int> &sessionTimes)
{
    const QString connName = QStringLiteral("test_stats_widget_setup");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dir.filePath(QLatin1StringView(PagesStatsWidget::FILENAME)));
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE displays_clicks ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  page_id TEXT NOT NULL,"
            "  display_at TEXT NOT NULL,"
            "  clicked_at TEXT)")));
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE page_session ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  page_id TEXT NOT NULL,"
            "  scrolling_percentage INTEGER NOT NULL,"
            "  time_on_page INTEGER NOT NULL,"
            "  is_final_page INTEGER NOT NULL)")));
        for (int i = 0; i < nDisplays; ++i) {
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO displays_clicks (page_id, display_at, clicked_at) "
                "VALUES ('/page', '2026-08-30', %1)")
                .arg(i < nClicked ? QStringLiteral("'2026-08-30'")
                                  : QStringLiteral("NULL"))));
        }
        for (const int seconds : sessionTimes) {
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO page_session "
                "(page_id, scrolling_percentage, time_on_page, is_final_page) "
                "VALUES ('/page', 80, %1, 1)").arg(seconds)));
        }
    }
    QSqlDatabase::removeDatabase(connName);
}

void Test_Website_PagesStatsWidget::addBotDisplays(const QDir &dir, int nBotDisplays)
{
    const QString connName = QStringLiteral("test_stats_widget_bots");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dir.filePath(QLatin1StringView(PagesStatsWidget::FILENAME)));
        QVERIFY(db.open());
        QSqlQuery q(db);
        q.exec(QStringLiteral(
            "ALTER TABLE displays_clicks ADD COLUMN is_bot INTEGER NOT NULL DEFAULT 0"));
        for (int i = 0; i < nBotDisplays; ++i) {
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO displays_clicks (page_id, display_at, is_bot) "
                "VALUES ('/page', '2026-08-30', 1)")));
        }
    }
    QSqlDatabase::removeDatabase(connName);
}

QVariant Test_Website_PagesStatsWidget::cellValue(PagesStatsWidget &widget, int column)
{
    auto *view = widget.findChild<QTableView *>();
    if (!view || !view->model() || view->model()->rowCount() < 1) {
        return {};
    }
    return view->model()->data(view->model()->index(0, column));
}

void Test_Website_PagesStatsWidget::test_statswidget_displays_not_multiplied_by_sessions()
{
    QTemporaryDir dir;
    writeStatsDb(QDir(dir.path()), 4, 0, {10, 20, 30}); // 4 displays, 3 sessions

    PagesStatsWidget widget{QDir(dir.path())};

    // With the fan-out bug this showed 4 × 3 = 12.
    QCOMPARE(cellValue(widget, 1).toInt(), 4);
}

void Test_Website_PagesStatsWidget::test_statswidget_clicks_counted_once()
{
    QTemporaryDir dir;
    writeStatsDb(QDir(dir.path()), 5, 2, {10, 20, 30});

    PagesStatsWidget widget{QDir(dir.path())};

    QCOMPARE(cellValue(widget, 2).toInt(), 2);          // not 2 × 3 sessions
    QCOMPARE(cellValue(widget, 3).toDouble(), 40.0);    // CTR = 2/5
}

void Test_Website_PagesStatsWidget::test_statswidget_avg_time_is_plain_session_average()
{
    QTemporaryDir dir;
    writeStatsDb(QDir(dir.path()), 7, 0, {10, 20}); // avg must be 15 regardless of displays

    PagesStatsWidget widget{QDir(dir.path())};

    QCOMPARE(cellValue(widget, 5).toDouble(), 15.0);
    QCOMPARE(cellValue(widget, 4).toDouble(), 80.0); // avg scroll
}

void Test_Website_PagesStatsWidget::test_statswidget_page_without_sessions_shows_zero_avgs()
{
    QTemporaryDir dir;
    writeStatsDb(QDir(dir.path()), 3, 0, {});

    PagesStatsWidget widget{QDir(dir.path())};

    QCOMPARE(cellValue(widget, 1).toInt(), 3); // displays survive the LEFT JOIN
    QCOMPARE(cellValue(widget, 4).toDouble(), 0.0);
    QCOMPARE(cellValue(widget, 5).toDouble(), 0.0);
}

void Test_Website_PagesStatsWidget::test_statswidget_bot_rows_excluded_by_default()
{
    QTemporaryDir dir;
    writeStatsDb(QDir(dir.path()), 3, 0, {10});
    addBotDisplays(QDir(dir.path()), 5);

    PagesStatsWidget widget{QDir(dir.path())};

    auto *check = widget.findChild<QCheckBox *>(QStringLiteral("checkExcludeBots"));
    QVERIFY(check);
    QVERIFY(check->isChecked()); // exclude-bots is the default
    QCOMPARE(cellValue(widget, 1).toInt(), 3); // 5 bot displays hidden
}

void Test_Website_PagesStatsWidget::test_statswidget_unchecking_filter_shows_bot_rows()
{
    QTemporaryDir dir;
    writeStatsDb(QDir(dir.path()), 3, 0, {10});
    addBotDisplays(QDir(dir.path()), 5);

    PagesStatsWidget widget{QDir(dir.path())};
    auto *check = widget.findChild<QCheckBox *>(QStringLiteral("checkExcludeBots"));
    QVERIFY(check);

    check->setChecked(false); // toggled signal triggers refresh()

    QCOMPARE(cellValue(widget, 1).toInt(), 8); // humans + bots
}

QTEST_MAIN(Test_Website_PagesStatsWidget)
#include "test_pages_stats_widget.moc"
