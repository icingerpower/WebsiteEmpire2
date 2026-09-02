#include "PagesStatsWidget.h"
#include "ui_PagesStatsWidget.h"

#include "website/perf/StatsDbMerger.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlQueryModel>

#include <atomic>

static std::atomic<int> s_statsConnCounter{0};

PagesStatsWidget::PagesStatsWidget(const QDir &workingDir, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PagesStatsWidget)
    , m_connectionName(QStringLiteral("stats_widget_")
                       + QString::number(s_statsConnCounter.fetch_add(1)))
    , m_model(new QSqlQueryModel(this))
{
    ui->setupUi(this);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                m_connectionName);
    db.setDatabaseName(workingDir.filePath(QLatin1StringView(FILENAME)));
    db.open();

    // Bootstrap minimal schema so the widget works even on a fresh stats.db.
    QSqlQuery q(QSqlDatabase::database(m_connectionName));
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS displays_clicks ("
        "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  page_id    TEXT    NOT NULL,"
        "  display_at TEXT    NOT NULL,"
        "  clicked_at TEXT,"
        "  is_bot     INTEGER NOT NULL DEFAULT 0 CHECK(is_bot IN (0,1))"
        ")"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS page_session ("
        "  id                   INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  page_id              TEXT    NOT NULL,"
        "  scrolling_percentage INTEGER NOT NULL CHECK(scrolling_percentage BETWEEN 0 AND 100),"
        "  time_on_page         INTEGER NOT NULL,"
        "  is_final_page        INTEGER NOT NULL CHECK(is_final_page IN (0,1)),"
        "  is_bot               INTEGER NOT NULL DEFAULT 0 CHECK(is_bot IN (0,1))"
        ")"));
    // Migrate a stats.db created before bot tracking (the "Exclude bot
    // traffic" filter needs the column to exist for its WHERE clause).
    QSqlDatabase existingDb = QSqlDatabase::database(m_connectionName);
    StatsDbMerger::ensureIsBotColumns(existingDb);

    ui->tableView->setModel(m_model);
    connect(ui->btnRefresh, &QPushButton::clicked, this, &PagesStatsWidget::refresh);
    connect(ui->checkExcludeBots, &QCheckBox::toggled, this, &PagesStatsWidget::refresh);

    refresh();
}

PagesStatsWidget::~PagesStatsWidget()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
    delete ui;
}

void PagesStatsWidget::refresh()
{
    // Bot rows (is_bot = 1, classified from the User-Agent by the server) are
    // excluded by default; rows recorded before bot tracking are unclassified
    // (is_bot = 0) and always count as human.
    const QString botFilter = ui->checkExcludeBots->isChecked()
                                  ? QStringLiteral(" WHERE is_bot = 0")
                                  : QString();

    // Aggregate each table SEPARATELY before joining.  Joining the raw rows
    // and then grouping (the obvious single-pass query) fans out: every
    // display row pairs with every session row of the same page, so Displays
    // becomes displays × sessions and the averages get display-weighted.
    m_model->setQuery(
        QStringLiteral(
            "SELECT"
            "  d.page_id             AS Permalink,"
            "  d.displays            AS Displays,"
            "  d.clicks              AS Clicks,"
            "  ROUND(100.0 * d.clicks / MAX(d.displays, 1), 1) AS CTR,"
            "  COALESCE(s.avg_scroll, 0) AS AvgScroll,"
            "  COALESCE(s.avg_time, 0)   AS AvgTime"
            " FROM (SELECT page_id,"
            "              COUNT(*) AS displays,"
            "              SUM(CASE WHEN clicked_at IS NOT NULL THEN 1 ELSE 0 END) AS clicks"
            "       FROM displays_clicks") + botFilter + QStringLiteral(" GROUP BY page_id) d"
            " LEFT JOIN (SELECT page_id,"
            "                   ROUND(AVG(scrolling_percentage), 1) AS avg_scroll,"
            "                   ROUND(AVG(time_on_page), 1)         AS avg_time"
            "            FROM page_session") + botFilter + QStringLiteral(" GROUP BY page_id) s"
            "   ON s.page_id = d.page_id"
            " ORDER BY Displays DESC"),
        QSqlDatabase::database(m_connectionName));

    m_model->setHeaderData(0, Qt::Horizontal, tr("Permalink"));
    m_model->setHeaderData(1, Qt::Horizontal, tr("Displays"));
    m_model->setHeaderData(2, Qt::Horizontal, tr("Clicks"));
    m_model->setHeaderData(3, Qt::Horizontal, tr("CTR%"));
    m_model->setHeaderData(4, Qt::Horizontal, tr("Avg Scroll%"));
    m_model->setHeaderData(5, Qt::Horizontal, tr("Avg Time(s)"));

    ui->tableView->resizeColumnsToContents();
    ui->lblRefreshed->setText(
        tr("Refreshed: %1")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"))));
}
