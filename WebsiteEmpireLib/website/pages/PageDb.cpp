#include "PageDb.h"

#include <QSqlError>
#include <QSqlQuery>

#include <atomic>

static std::atomic<int> s_connectionCounter{0};

PageDb::PageDb(const QDir &workingDir)
    : m_connectionName(QStringLiteral("page_db_")
                       + QString::number(s_connectionCounter.fetch_add(1)))
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(workingDir.filePath(QLatin1StringView(FILENAME)));

    const bool opened = db.open();
    Q_ASSERT_X(opened, "PageDb", qPrintable(db.lastError().text()));

    createSchema();
}

PageDb::~PageDb()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

QSqlDatabase PageDb::database() const
{
    return QSqlDatabase::database(m_connectionName);
}

const QString &PageDb::connectionName() const
{
    return m_connectionName;
}

void PageDb::createSchema()
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName));

    q.exec(QStringLiteral("PRAGMA foreign_keys = ON"));

    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS pages ("
        "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  type_id    TEXT    NOT NULL,"
        "  permalink  TEXT    UNIQUE NOT NULL,"
        "  lang       TEXT    NOT NULL,"
        "  created_at TEXT    NOT NULL,"
        "  updated_at TEXT    NOT NULL"
        ")"));

    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS page_data ("
        "  page_id INTEGER NOT NULL REFERENCES pages(id) ON DELETE CASCADE,"
        "  key     TEXT    NOT NULL,"
        "  value   TEXT    NOT NULL,"
        "  PRIMARY KEY (page_id, key)"
        ")"));

    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS permalink_history ("
        "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  page_id       INTEGER NOT NULL REFERENCES pages(id) ON DELETE CASCADE,"
        "  old_permalink TEXT    NOT NULL,"
        "  changed_at    TEXT    NOT NULL"
        ")"));
}
