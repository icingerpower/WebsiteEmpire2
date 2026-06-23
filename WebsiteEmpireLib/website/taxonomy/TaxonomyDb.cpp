#include "TaxonomyDb.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QDebug>

static int s_seed = 0;

TaxonomyDb::TaxonomyDb(const QDir &workingDir)
    : m_dbPath(workingDir.filePath(QStringLiteral("taxonomy/taxonomy.db")))
{
}

void TaxonomyDb::_ensureSchema() const
{
    // Ensure the taxonomy/ directory exists.
    const QString dirPath = QFileInfo(m_dbPath).absolutePath();
    QDir().mkpath(dirPath);

    const QString connName = QStringLiteral("taxdb_schema_") + QString::number(++s_seed);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(m_dbPath);
        if (!db.open()) {
            qWarning() << "TaxonomyDb: cannot open" << m_dbPath << db.lastError().text();
        } else {
            QSqlQuery q(db);
            q.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS items("
                "type TEXT NOT NULL,"
                "name TEXT NOT NULL,"
                "PRIMARY KEY(type, name))"));
        }
    }
    QSqlDatabase::removeDatabase(connName);
}

void TaxonomyDb::sync(const QString &type, const QStringList &names)
{
    _ensureSchema();

    const QString connName = QStringLiteral("taxdb_") + QString::number(++s_seed);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(m_dbPath);
        if (!db.open()) {
            qWarning() << "TaxonomyDb::sync: cannot open" << m_dbPath << db.lastError().text();
        } else {
            db.transaction();
            {
                QSqlQuery del(db);
                del.prepare(QStringLiteral("DELETE FROM items WHERE type = :type"));
                del.bindValue(QStringLiteral(":type"), type);
                del.exec();
            }
            {
                QSqlQuery ins(db);
                ins.prepare(QStringLiteral("INSERT INTO items(type, name) VALUES(:type, :name)"));
                for (const QString &name : std::as_const(names)) {
                    ins.bindValue(QStringLiteral(":type"), type);
                    ins.bindValue(QStringLiteral(":name"), name);
                    ins.exec();
                }
            }
            db.commit();
        }
    }
    QSqlDatabase::removeDatabase(connName);
}

QStringList TaxonomyDb::load(const QString &type) const
{
    _ensureSchema();

    QStringList result;
    const QString connName = QStringLiteral("taxdb_") + QString::number(++s_seed);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(m_dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "SELECT name FROM items WHERE type = :type ORDER BY name COLLATE NOCASE"));
            q.bindValue(QStringLiteral(":type"), type);
            q.exec();
            while (q.next()) {
                result.append(q.value(0).toString());
            }
        }
    }
    QSqlDatabase::removeDatabase(connName);
    return result;
}

int TaxonomyDb::count(const QString &type) const
{
    _ensureSchema();

    int result = 0;
    const QString connName = QStringLiteral("taxdb_") + QString::number(++s_seed);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(m_dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT COUNT(*) FROM items WHERE type = :type"));
            q.bindValue(QStringLiteral(":type"), type);
            if (q.exec() && q.next()) {
                result = q.value(0).toInt();
            }
        }
    }
    QSqlDatabase::removeDatabase(connName);
    return result;
}
