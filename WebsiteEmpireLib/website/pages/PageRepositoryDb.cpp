#include "PageRepositoryDb.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>

PageRepositoryDb::PageRepositoryDb(PageDb &db)
    : m_db(db)
{
}

// =============================================================================
// Helpers
// =============================================================================

QString PageRepositoryDb::currentUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

static PageRecord rowToRecord(const QSqlQuery &q)
{
    PageRecord r;
    r.id        = q.value(0).toInt();
    r.typeId    = q.value(1).toString();
    r.permalink = q.value(2).toString();
    r.lang      = q.value(3).toString();
    r.createdAt = q.value(4).toString();
    r.updatedAt = q.value(5).toString();
    return r;
}

// =============================================================================
// create
// =============================================================================

int PageRepositoryDb::create(const QString &typeId,
                              const QString &permalink,
                              const QString &lang)
{
    const QString &now = currentUtc();
    QSqlDatabase db = m_db.database();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO pages (type_id, permalink, lang, created_at, updated_at)"
        " VALUES (:type_id, :permalink, :lang, :created_at, :updated_at)"));
    q.bindValue(QStringLiteral(":type_id"),    typeId);
    q.bindValue(QStringLiteral(":permalink"),  permalink);
    q.bindValue(QStringLiteral(":lang"),       lang);
    q.bindValue(QStringLiteral(":created_at"), now);
    q.bindValue(QStringLiteral(":updated_at"), now);
    q.exec();
    return q.lastInsertId().toInt();
}

// =============================================================================
// findById / findAll
// =============================================================================

std::optional<PageRecord> PageRepositoryDb::findById(int id) const
{
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral(
        "SELECT id, type_id, permalink, lang, created_at, updated_at"
        " FROM pages WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    q.exec();
    if (q.next()) {
        return rowToRecord(q);
    }
    return std::nullopt;
}

QList<PageRecord> PageRepositoryDb::findAll() const
{
    QList<PageRecord> result;
    QSqlQuery q(m_db.database());
    q.exec(QStringLiteral(
        "SELECT id, type_id, permalink, lang, created_at, updated_at"
        " FROM pages ORDER BY id ASC"));
    while (q.next()) {
        result.append(rowToRecord(q));
    }
    return result;
}

// =============================================================================
// saveData / loadData
// =============================================================================

void PageRepositoryDb::saveData(int id, const QHash<QString, QString> &data)
{
    QSqlDatabase db = m_db.database();
    db.transaction();

    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM page_data WHERE page_id = :id"));
    del.bindValue(QStringLiteral(":id"), id);
    del.exec();

    QSqlQuery ins(db);
    ins.prepare(QStringLiteral(
        "INSERT INTO page_data (page_id, key, value) VALUES (:page_id, :key, :value)"));
    for (auto it = data.cbegin(); it != data.cend(); ++it) {
        ins.bindValue(QStringLiteral(":page_id"), id);
        ins.bindValue(QStringLiteral(":key"),     it.key());
        ins.bindValue(QStringLiteral(":value"),   it.value());
        ins.exec();
    }

    QSqlQuery upd(db);
    upd.prepare(QStringLiteral(
        "UPDATE pages SET updated_at = :now WHERE id = :id"));
    upd.bindValue(QStringLiteral(":now"), currentUtc());
    upd.bindValue(QStringLiteral(":id"),  id);
    upd.exec();

    db.commit();
}

QHash<QString, QString> PageRepositoryDb::loadData(int id) const
{
    QHash<QString, QString> result;
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral(
        "SELECT key, value FROM page_data WHERE page_id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    q.exec();
    while (q.next()) {
        result.insert(q.value(0).toString(), q.value(1).toString());
    }
    return result;
}

// =============================================================================
// remove
// =============================================================================

void PageRepositoryDb::remove(int id)
{
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral("DELETE FROM pages WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    q.exec();
}

// =============================================================================
// updatePermalink / permalinkHistory
// =============================================================================

void PageRepositoryDb::updatePermalink(int id, const QString &newPermalink)
{
    const auto &current = findById(id);
    if (!current || current->permalink == newPermalink) {
        return;
    }

    QSqlDatabase db = m_db.database();
    db.transaction();

    const QString &now = currentUtc();

    QSqlQuery hist(db);
    hist.prepare(QStringLiteral(
        "INSERT INTO permalink_history (page_id, old_permalink, changed_at)"
        " VALUES (:page_id, :old_permalink, :changed_at)"));
    hist.bindValue(QStringLiteral(":page_id"),       id);
    hist.bindValue(QStringLiteral(":old_permalink"), current->permalink);
    hist.bindValue(QStringLiteral(":changed_at"),    now);
    hist.exec();

    QSqlQuery upd(db);
    upd.prepare(QStringLiteral(
        "UPDATE pages SET permalink = :permalink, updated_at = :now WHERE id = :id"));
    upd.bindValue(QStringLiteral(":permalink"), newPermalink);
    upd.bindValue(QStringLiteral(":now"),       now);
    upd.bindValue(QStringLiteral(":id"),        id);
    upd.exec();

    db.commit();
}

QList<QString> PageRepositoryDb::permalinkHistory(int id) const
{
    QList<QString> result;
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral(
        "SELECT old_permalink FROM permalink_history"
        " WHERE page_id = :id ORDER BY changed_at ASC"));
    q.bindValue(QStringLiteral(":id"), id);
    q.exec();
    while (q.next()) {
        result.append(q.value(0).toString());
    }
    return result;
}
