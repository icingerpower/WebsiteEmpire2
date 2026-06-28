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
            q.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS translations("
                "type            TEXT NOT NULL,"
                "name            TEXT NOT NULL,"
                "lang_code       TEXT NOT NULL,"
                "translated_name TEXT NOT NULL,"
                "PRIMARY KEY(type, name, lang_code))"));
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

QStringList TaxonomyDb::allTypes() const
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
            q.exec(QStringLiteral(
                "SELECT DISTINCT type FROM items ORDER BY type COLLATE NOCASE"));
            while (q.next()) {
                result.append(q.value(0).toString());
            }
        }
    }
    QSqlDatabase::removeDatabase(connName);
    return result;
}

void TaxonomyDb::setTranslation(const QString &type, const QString &name,
                                 const QString &langCode, const QString &translatedName)
{
    _ensureSchema();

    const QString connName = QStringLiteral("taxdb_") + QString::number(++s_seed);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(m_dbPath);
        if (db.open()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "INSERT INTO translations(type, name, lang_code, translated_name)"
                " VALUES(:type, :name, :lang, :tr)"
                " ON CONFLICT(type, name, lang_code)"
                " DO UPDATE SET translated_name = excluded.translated_name"));
            q.bindValue(QStringLiteral(":type"), type);
            q.bindValue(QStringLiteral(":name"), name);
            q.bindValue(QStringLiteral(":lang"),  langCode);
            q.bindValue(QStringLiteral(":tr"),    translatedName);
            q.exec();
        }
    }
    QSqlDatabase::removeDatabase(connName);
}

QString TaxonomyDb::translationFor(const QString &type, const QString &name,
                                    const QString &langCode) const
{
    _ensureSchema();

    QString result = name;
    const QString connName = QStringLiteral("taxdb_") + QString::number(++s_seed);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(m_dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "SELECT translated_name FROM translations"
                " WHERE type = :type AND name = :name AND lang_code = :lang"));
            q.bindValue(QStringLiteral(":type"), type);
            q.bindValue(QStringLiteral(":name"), name);
            q.bindValue(QStringLiteral(":lang"),  langCode);
            if (q.exec() && q.next()) {
                const QString &tr = q.value(0).toString();
                if (!tr.isEmpty()) {
                    result = tr;
                }
            }
        }
    }
    QSqlDatabase::removeDatabase(connName);
    return result;
}

QStringList TaxonomyDb::loadUntranslated(const QString &type, const QString &langCode) const
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
                "SELECT i.name FROM items i"
                " WHERE i.type = :type"
                "   AND NOT EXISTS ("
                "       SELECT 1 FROM translations t"
                "       WHERE t.type = i.type AND t.name = i.name AND t.lang_code = :lang)"
                " ORDER BY i.name COLLATE NOCASE"));
            q.bindValue(QStringLiteral(":type"), type);
            q.bindValue(QStringLiteral(":lang"),  langCode);
            q.exec();
            while (q.next()) {
                result.append(q.value(0).toString());
            }
        }
    }
    QSqlDatabase::removeDatabase(connName);
    return result;
}

QList<QPair<QString, QString>> TaxonomyDb::loadTranslated(const QString &type,
                                                            const QString &langCode) const
{
    _ensureSchema();

    QList<QPair<QString, QString>> result;
    const QString connName = QStringLiteral("taxdb_") + QString::number(++s_seed);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(m_dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery q(db);
            // Order by the display name (translated when available, English otherwise).
            q.prepare(QStringLiteral(
                "SELECT i.name,"
                "       COALESCE(NULLIF(t.translated_name, ''), i.name) AS display"
                " FROM items i"
                " LEFT JOIN translations t"
                "        ON t.type = i.type AND t.name = i.name AND t.lang_code = :lang"
                " WHERE i.type = :type"
                " ORDER BY display COLLATE NOCASE"));
            q.bindValue(QStringLiteral(":lang"),  langCode);
            q.bindValue(QStringLiteral(":type"), type);
            q.exec();
            while (q.next()) {
                result.append({q.value(0).toString(), q.value(1).toString()});
            }
        }
    }
    QSqlDatabase::removeDatabase(connName);
    return result;
}
