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
    r.id           = q.value(0).toInt();
    r.typeId       = q.value(1).toString();
    r.permalink    = q.value(2).toString();
    r.lang         = q.value(3).toString();
    r.createdAt    = q.value(4).toString();
    r.updatedAt    = q.value(5).toString();
    r.translatedAt = q.value(6).isNull() ? QString() : q.value(6).toString();
    r.sourcePageId = q.value(7).isNull() ? 0        : q.value(7).toInt();
    r.generatedAt  = q.value(8).isNull() ? QString() : q.value(8).toString();
    return r;
}

static const QLatin1StringView SELECT_PAGES{
    "SELECT id, type_id, permalink, lang, created_at, updated_at,"
    "       translated_at, source_page_id, generated_at"
    " FROM pages"};

// =============================================================================
// create / createTranslation
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
        " VALUES (:type_id, :permalink, :lang, :now, :now2)"));
    q.bindValue(QStringLiteral(":type_id"),   typeId);
    q.bindValue(QStringLiteral(":permalink"), permalink);
    q.bindValue(QStringLiteral(":lang"),      lang);
    q.bindValue(QStringLiteral(":now"),       now);
    q.bindValue(QStringLiteral(":now2"),      now);
    q.exec();
    return q.lastInsertId().toInt();
}

int PageRepositoryDb::createTranslation(int           sourcePageId,
                                         const QString &typeId,
                                         const QString &permalink,
                                         const QString &lang)
{
    const QString &now = currentUtc();
    QSqlDatabase db = m_db.database();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO pages"
        "  (type_id, permalink, lang, created_at, updated_at, source_page_id)"
        " VALUES (:type_id, :permalink, :lang, :now, :now2, :src)"));
    q.bindValue(QStringLiteral(":type_id"),   typeId);
    q.bindValue(QStringLiteral(":permalink"), permalink);
    q.bindValue(QStringLiteral(":lang"),      lang);
    q.bindValue(QStringLiteral(":now"),       now);
    q.bindValue(QStringLiteral(":now2"),      now);
    q.bindValue(QStringLiteral(":src"),       sourcePageId);
    q.exec();
    return q.lastInsertId().toInt();
}

// =============================================================================
// findById / findAll / findSourcePages / findTranslations
// =============================================================================

std::optional<PageRecord> PageRepositoryDb::findById(int id) const
{
    QSqlQuery q(m_db.database());
    q.prepare(QString::fromLatin1(SELECT_PAGES) + QStringLiteral(" WHERE id = :id"));
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
    q.exec(QString::fromLatin1(SELECT_PAGES) + QStringLiteral(" ORDER BY id ASC"));
    while (q.next()) {
        result.append(rowToRecord(q));
    }
    return result;
}

QList<PageRecord> PageRepositoryDb::findSourcePages(const QString &lang) const
{
    QList<PageRecord> result;
    QSqlQuery q(m_db.database());
    q.prepare(QString::fromLatin1(SELECT_PAGES)
              + QStringLiteral(" WHERE source_page_id IS NULL AND lang = :lang"
                               " ORDER BY id ASC"));
    q.bindValue(QStringLiteral(":lang"), lang);
    q.exec();
    while (q.next()) {
        result.append(rowToRecord(q));
    }
    return result;
}

QList<PageRecord> PageRepositoryDb::findTranslations(int sourcePageId) const
{
    QList<PageRecord> result;
    QSqlQuery q(m_db.database());
    q.prepare(QString::fromLatin1(SELECT_PAGES)
              + QStringLiteral(" WHERE source_page_id = :src ORDER BY id ASC"));
    q.bindValue(QStringLiteral(":src"), sourcePageId);
    q.exec();
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
// updatePermalink / permalinkHistory / setHistoryRedirectType
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

QList<PermalinkHistoryEntry> PageRepositoryDb::permalinkHistory(int id) const
{
    QList<PermalinkHistoryEntry> result;
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral(
        "SELECT id, page_id, old_permalink, changed_at,"
        "       COALESCE(redirect_type, 'permanent') AS redirect_type"
        " FROM permalink_history"
        " WHERE page_id = :id ORDER BY changed_at ASC"));
    q.bindValue(QStringLiteral(":id"), id);
    q.exec();
    while (q.next()) {
        PermalinkHistoryEntry e;
        e.id           = q.value(0).toInt();
        e.pageId       = q.value(1).toInt();
        e.permalink    = q.value(2).toString();
        e.changedAt    = q.value(3).toString();
        e.redirectType = q.value(4).toString();
        result.append(e);
    }
    return result;
}

void PageRepositoryDb::setHistoryRedirectType(int historyEntryId, const QString &type)
{
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral(
        "UPDATE permalink_history SET redirect_type = :type WHERE id = :id"));
    q.bindValue(QStringLiteral(":type"), type);
    q.bindValue(QStringLiteral(":id"),   historyEntryId);
    q.exec();
}

// =============================================================================
// translatedAt / setTranslatedAt
// =============================================================================

QString PageRepositoryDb::translatedAt(int id) const
{
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral(
        "SELECT translated_at FROM pages WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    q.exec();
    if (q.next()) {
        return q.value(0).isNull() ? QString() : q.value(0).toString();
    }
    return {};
}

void PageRepositoryDb::setTranslatedAt(int id, const QString &utcIso)
{
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral(
        "UPDATE pages SET translated_at = :ts WHERE id = :id"));
    q.bindValue(QStringLiteral(":ts"), utcIso);
    q.bindValue(QStringLiteral(":id"), id);
    q.exec();
}

// =============================================================================
// AI content generation tracking
// =============================================================================

QList<PageRecord> PageRepositoryDb::findPendingByTypeId(const QString &typeId) const
{
    QList<PageRecord> result;
    QSqlQuery q(m_db.database());
    // Source pages (source_page_id IS NULL) of the given type whose
    // generated_at is NULL and whose page_data is empty.  Pages with
    // manually-written content (page_data non-empty) are excluded so the
    // launcher never overwrites human edits.
    q.prepare(QString::fromLatin1(SELECT_PAGES)
              + QStringLiteral(
                  " WHERE type_id = :typeId"
                  "   AND source_page_id IS NULL"
                  "   AND generated_at IS NULL"
                  "   AND NOT EXISTS ("
                  "         SELECT 1 FROM page_data pd WHERE pd.page_id = pages.id)"
                  " ORDER BY id ASC"));
    q.bindValue(QStringLiteral(":typeId"), typeId);
    q.exec();
    while (q.next()) {
        result.append(rowToRecord(q));
    }
    return result;
}

int PageRepositoryDb::countByTypeId(const QString &typeId) const
{
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM pages"
        " WHERE type_id = :typeId AND source_page_id IS NULL"));
    q.bindValue(QStringLiteral(":typeId"), typeId);
    q.exec();
    if (q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

void PageRepositoryDb::setGeneratedAt(int id, const QString &utcIso)
{
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral(
        "UPDATE pages SET generated_at = :ts WHERE id = :id"));
    q.bindValue(QStringLiteral(":ts"), utcIso);
    q.bindValue(QStringLiteral(":id"), id);
    q.exec();
}
