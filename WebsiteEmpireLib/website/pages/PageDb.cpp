#include "PageDb.h"

#include <QSqlError>
#include <QSqlQuery>

#include <atomic>

static std::atomic<int> s_connectionCounter{0};

// ---------------------------------------------------------------------------
// Migration helpers (file-static — not visible outside this translation unit)
// ---------------------------------------------------------------------------

static bool columnExists(const QString &connName,
                          const QString &tableName,
                          const QString &columnName)
{
    QSqlQuery q(QSqlDatabase::database(connName));
    q.exec(QStringLiteral("PRAGMA table_info(") + tableName + QStringLiteral(")"));
    while (q.next()) {
        if (q.value(1).toString() == columnName) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------

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
        "  id             INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  type_id        TEXT    NOT NULL,"
        "  permalink      TEXT    UNIQUE NOT NULL,"
        "  lang           TEXT    NOT NULL,"
        "  created_at     TEXT    NOT NULL,"
        "  updated_at     TEXT    NOT NULL"
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

    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS strategy_attempts ("
        "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  page_id      INTEGER NOT NULL REFERENCES pages(id) ON DELETE CASCADE,"
        "  strategy_id  TEXT    NOT NULL,"
        "  attempted_at TEXT    NOT NULL"
        ")"));

    // ---- Schema migrations (non-destructive ALTER TABLE ADD COLUMN) --------

    // pages.translated_at — ISO 8601 UTC; NULL until AI translation completes
    if (!columnExists(m_connectionName, QStringLiteral("pages"),
                      QStringLiteral("translated_at"))) {
        q.exec(QStringLiteral(
            "ALTER TABLE pages ADD COLUMN translated_at TEXT"));
    }

    // pages.source_page_id — NULL for root pages, FK to source for translations
    if (!columnExists(m_connectionName, QStringLiteral("pages"),
                      QStringLiteral("source_page_id"))) {
        q.exec(QStringLiteral(
            "ALTER TABLE pages ADD COLUMN source_page_id INTEGER REFERENCES pages(id)"));
    }

    // permalink_history.redirect_type — controls how PageGenerator emits redirects
    if (!columnExists(m_connectionName, QStringLiteral("permalink_history"),
                      QStringLiteral("redirect_type"))) {
        q.exec(QStringLiteral(
            "ALTER TABLE permalink_history"
            " ADD COLUMN redirect_type TEXT NOT NULL DEFAULT 'permanent'"));
    }

    // pages.generated_at — ISO 8601 UTC; NULL until LauncherGeneration fills content
    if (!columnExists(m_connectionName, QStringLiteral("pages"),
                      QStringLiteral("generated_at"))) {
        q.exec(QStringLiteral(
            "ALTER TABLE pages ADD COLUMN generated_at TEXT"));
    }

    // pages.langs_to_translate — comma-separated BCP-47 codes set by the
    // assessment step.  NULL / empty = author language only.
    if (!columnExists(m_connectionName, QStringLiteral("pages"),
                      QStringLiteral("langs_to_translate"))) {
        q.exec(QStringLiteral(
            "ALTER TABLE pages ADD COLUMN langs_to_translate TEXT"));
    }
}
