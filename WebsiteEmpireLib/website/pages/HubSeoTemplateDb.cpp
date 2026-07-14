#include "HubSeoTemplateDb.h"

#include <QSqlDatabase>
#include <QSqlQuery>

static int s_seed = 0;

HubSeoTemplateDb::HubSeoTemplateDb(const QDir &workingDir)
    : m_connName(QStringLiteral("hub_seo_db_") + QString::number(++s_seed))
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
    db.setDatabaseName(workingDir.filePath(QStringLiteral("hub_seo.db")));
    if (db.open()) {
        _ensureSchema();
    }
}

HubSeoTemplateDb::~HubSeoTemplateDb()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connName);
        db.close();
    }
    QSqlDatabase::removeDatabase(m_connName);
}

void HubSeoTemplateDb::_ensureSchema()
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS hub_seo_templates ("
        "  type_id   TEXT NOT NULL,"
        "  key       TEXT NOT NULL,"
        "  lang_code TEXT NOT NULL,"
        "  template  TEXT NOT NULL,"
        "  PRIMARY KEY (type_id, key, lang_code)"
        ")"));
}

QString HubSeoTemplateDb::get(const QString &typeId,
                               const QString &key,
                               const QString &langCode) const
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    if (!db.isOpen()) {
        return {};
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT template FROM hub_seo_templates"
        " WHERE type_id=:t AND key=:k AND lang_code=:l"));
    q.bindValue(QStringLiteral(":t"), typeId);
    q.bindValue(QStringLiteral(":k"), key);
    q.bindValue(QStringLiteral(":l"), langCode);
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    return {};
}

void HubSeoTemplateDb::set(const QString &typeId,
                             const QString &key,
                             const QString &langCode,
                             const QString &templ)
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    if (!db.isOpen()) {
        return;
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO hub_seo_templates (type_id, key, lang_code, template)"
        " VALUES (:t, :k, :l, :v)"));
    q.bindValue(QStringLiteral(":t"), typeId);
    q.bindValue(QStringLiteral(":k"), key);
    q.bindValue(QStringLiteral(":l"), langCode);
    q.bindValue(QStringLiteral(":v"), templ);
    q.exec();
}

QHash<QString, QHash<QString, QString>>
HubSeoTemplateDb::loadAll(const QString &typeId) const
{
    QHash<QString, QHash<QString, QString>> result;
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    if (!db.isOpen()) {
        return result;
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT key, lang_code, template FROM hub_seo_templates WHERE type_id=:t"));
    q.bindValue(QStringLiteral(":t"), typeId);
    if (q.exec()) {
        while (q.next()) {
            result[q.value(0).toString()][q.value(1).toString()] = q.value(2).toString();
        }
    }
    return result;
}

QList<HubSeoTemplateDb::StoredKey> HubSeoTemplateDb::allStoredKeys() const
{
    QList<StoredKey> keys;
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    if (!db.isOpen()) {
        return keys;
    }
    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "SELECT type_id, key, lang_code FROM hub_seo_templates"));
    while (q.next()) {
        keys.append({ q.value(0).toString(),
                      q.value(1).toString(),
                      q.value(2).toString() });
    }
    return keys;
}
