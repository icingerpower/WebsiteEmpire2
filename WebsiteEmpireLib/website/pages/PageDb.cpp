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

    // WAL mode allows readers and writers to run concurrently — no reader ever
    // blocks a writer and no writer ever blocks a reader.  Without this, a
    // concurrent update run holding a write lock causes findAll() to silently
    // return an empty list, making generation treat all existing pages as new.
    // busy_timeout is kept as a safety net for the brief exclusive-lock window
    // during WAL checkpoints.
    QSqlQuery pragmaQ(QSqlDatabase::database(m_connectionName));
    pragmaQ.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    pragmaQ.exec(QStringLiteral("PRAGMA busy_timeout = 10000"));

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

    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS update_attempts ("
        "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  page_id     INTEGER NOT NULL REFERENCES pages(id) ON DELETE CASCADE,"
        "  prompt_id   TEXT    NOT NULL,"
        "  updated_at  TEXT    NOT NULL"
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

    // pages.generation_state — PageGenerationState enum (Pending=0 … Complete=3).
    // Legacy rows with generated_at IS NOT NULL are promoted to Complete so the
    // translation guard (state must be Complete) does not block existing content.
    if (!columnExists(m_connectionName, QStringLiteral("pages"),
                      QStringLiteral("generation_state"))) {
        q.exec(QStringLiteral(
            "ALTER TABLE pages ADD COLUMN generation_state INTEGER NOT NULL DEFAULT 0"));
        q.exec(QStringLiteral(
            "UPDATE pages SET generation_state = 3 WHERE generated_at IS NOT NULL"));
    }

    // pages.flags — PageFlag bitmask; 0 = no flags set.
    if (!columnExists(m_connectionName, QStringLiteral("pages"),
                      QStringLiteral("flags"))) {
        q.exec(QStringLiteral(
            "ALTER TABLE pages ADD COLUMN flags INTEGER NOT NULL DEFAULT 0"));
    }

    // pages.end_permalink — URL slug suffix from the generation strategy.
    // Empty string for pages created before this field existed or without a suffix.
    if (!columnExists(m_connectionName, QStringLiteral("pages"),
                      QStringLiteral("end_permalink"))) {
        q.exec(QStringLiteral(
            "ALTER TABLE pages ADD COLUMN end_permalink TEXT NOT NULL DEFAULT ''"));
    }

    // pages.published_at — ISO 8601 UTC; NULL until the page is first deployed
    // via PaneDomains::deployLocally().  History entries are only created when
    // this is non-NULL so that unpublished permalink changes leave no redirect trail.
    if (!columnExists(m_connectionName, QStringLiteral("pages"),
                      QStringLiteral("published_at"))) {
        q.exec(QStringLiteral(
            "ALTER TABLE pages ADD COLUMN published_at TEXT"));
    }

    // page_translation_image_states — per-language social image generation state.
    // Pending (0): social SVGs / WebPs not yet translated for this language.
    // Complete (3): all social image variants translated and written to images.db.
    // LauncherUpdate resets rows to Pending when the source SVG changes, so
    // outdated blobs are silently overwritten on the next translation run.
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS page_translation_image_states ("
        "  page_id INTEGER NOT NULL REFERENCES pages(id) ON DELETE CASCADE,"
        "  lang    TEXT    NOT NULL,"
        "  state   INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (page_id, lang)"
        ")"));
}
