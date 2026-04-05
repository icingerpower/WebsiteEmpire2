#include "PagesStatsWidget.h"
#include "ui_PagesStatsWidget.h"

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
        "  clicked_at TEXT"
        ")"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS page_session ("
        "  id                   INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  page_id              TEXT    NOT NULL,"
        "  scrolling_percentage INTEGER NOT NULL CHECK(scrolling_percentage BETWEEN 0 AND 100),"
        "  time_on_page         INTEGER NOT NULL,"
        "  is_final_page        INTEGER NOT NULL CHECK(is_final_page IN (0,1))"
        ")"));

    ui->tableView->setModel(m_model);
    connect(ui->btnRefresh, &QPushButton::clicked, this, &PagesStatsWidget::refresh);

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
    // One SQL query aggregates both tables in a single pass.
    m_model->setQuery(
        QStringLiteral(
            "SELECT"
            "  d.page_id                                    AS Permalink,"
            "  COUNT(d.id)                                  AS Displays,"
            "  SUM(CASE WHEN d.clicked_at IS NOT NULL THEN 1 ELSE 0 END) AS Clicks,"
            "  ROUND(100.0 * SUM(CASE WHEN d.clicked_at IS NOT NULL THEN 1 ELSE 0 END)"
            "        / MAX(COUNT(d.id), 1), 1)              AS CTR,"
            "  COALESCE(ROUND(AVG(s.scrolling_percentage),1), 0) AS AvgScroll,"
            "  COALESCE(ROUND(AVG(s.time_on_page),1), 0)    AS AvgTime"
            " FROM displays_clicks d"
            " LEFT JOIN page_session s ON s.page_id = d.page_id"
            " GROUP BY d.page_id"
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
