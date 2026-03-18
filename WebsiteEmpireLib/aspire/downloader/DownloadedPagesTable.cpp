#include "DownloadedPagesTable.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QMetaObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>

#include "AbstractDownloader.h"
#include "ExceptionWithTitleText.h"

// Opens a dedicated read connection for QSqlTableModel to the same .db file
// that AspiredDb will write to.  Called inside the member-initialiser list so
// the resulting QSqlDatabase can be forwarded to the QSqlTableModel ctor.
static QSqlDatabase openViewConnection(const QDir &workingDir,
                                       const QString &downloaderId)
{
    const QString connName = downloaderId + "_view_"
                             + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
    db.setDatabaseName(workingDir.filePath(downloaderId + ".db"));
    if (!db.open()) {
        QSqlDatabase::removeDatabase(connName);
        ExceptionWithTitleText ex(
            QObject::tr("Database Error"),
            QObject::tr("Failed to open view connection for '%1': %2")
                .arg(downloaderId, db.lastError().text()));
        ex.raise();
    }
    return db;
}

DownloadedPagesTable::DownloadedPagesTable(const QDir &workingDir,
                                           AbstractDownloader *downloader,
                                           QObject *parent)
    : QSqlTableModel(parent, openViewConnection(workingDir, downloader->getId()))
    , m_aspiredDb(workingDir.path(), downloader->getId())
    , m_downloader(downloader)
    , m_pageAttributes(downloader->createPageAttributes())
{
    const auto &attrsPtr = m_pageAttributes->getAttributes();
    if (attrsPtr) {
        m_attributes = *attrsPtr;
    }
    for (const auto &attr : std::as_const(m_attributes)) {
        if (attr.isImage) {
            m_imageAttributeIds.insert(attr.id);
        }
    }

    // Ensure the table and all attribute columns exist before setTable() is
    // called.  QSqlTableModel fetches its column metadata in setTable(); if the
    // table does not exist at that point the column count stays zero for the
    // lifetime of the model even after subsequent select() calls.
    m_aspiredDb.createTableIdNeed(m_attributes);

    setTable(AspiredDb::TABLE_NAME);
    // Writes are managed exclusively through m_aspiredDb; prevent the model
    // from submitting any in-place edits automatically.
    setEditStrategy(QSqlTableModel::OnManualSubmit);
    select();
}

DownloadedPagesTable::~DownloadedPagesTable()
{
    // QSqlTableModel stores the connection in its private d->db member, which
    // is only destroyed when the base-class destructor runs — after our
    // destructor.  Calling removeDatabase() here would therefore always
    // trigger a "still in use" warning because d->db still holds a reference.
    //
    // Fix: release the model's query cache (setQuery / setTable), then
    // schedule the actual database close+remove as a queued event.  By the
    // time that event fires — in the next event-loop iteration after the full
    // destructor chain — d->db has been destroyed and no references remain.
    const QString connName = database().connectionName();
    setQuery(QSqlQuery());
    setTable(QString{});
    QMetaObject::invokeMethod(QCoreApplication::instance(), [connName]() {
        QSqlDatabase db = QSqlDatabase::database(connName, false);
        if (db.isValid()) {
            db.close();
        }
        QSqlDatabase::removeDatabase(connName);
    }, Qt::QueuedConnection);
}

AbstractDownloader *DownloadedPagesTable::downloader() const
{
    return m_downloader;
}

bool DownloadedPagesTable::select()
{
    // Build a SELECT that avoids loading full image BLOBs.  For image columns
    // we fetch only the first 4 bytes (the QDataStream-serialised qint32 image
    // count) so data() can display "N images" without touching the pixel data.
    // Full BLOBs are still read on-demand through imagesAt().
    QStringList cols;
    cols.reserve(m_attributes.size() + 1);
    cols << QStringLiteral("id");
    for (const auto &attr : std::as_const(m_attributes)) {
        if (m_imageAttributeIds.contains(attr.id)) {
            cols << QStringLiteral("substr(%1, 1, 4) AS %1").arg(attr.id);
        } else {
            cols << attr.id;
        }
    }

    const QString sql = QString("SELECT %1 FROM %2")
                            .arg(cols.join(QStringLiteral(", ")),
                                 AspiredDb::TABLE_NAME);
    QSqlQuery q(database());
    q.exec(sql);
    // setQuery() puts QSqlTableModel into raw-query mode; writes still go
    // exclusively through m_aspiredDb so this is safe.
    QSqlTableModel::setQuery(std::move(q));
    return !lastError().isValid();
}

void DownloadedPagesTable::recordPage(
    const QHash<QString, QString> &idAttr_value,
    const QHash<QString, QList<QSharedPointer<QImage>>> &idAttr_imageValue)
{
    // Throws ExceptionWithTitleText on validation failure — the model is left
    // unchanged in that case.
    m_aspiredDb.record(m_attributes, idAttr_value, m_pageAttributes.get(),
                       idAttr_imageValue);
    select();
}

void DownloadedPagesTable::updatePage(
    const QString &rowId,
    const QHash<QString, QString> &idAttr_value,
    const QHash<QString, QList<QSharedPointer<QImage>>> &idAttr_imageValue)
{
    // Throws ExceptionWithTitleText on validation failure — the row is left
    // unchanged in that case.
    m_aspiredDb.update(rowId, m_attributes, idAttr_value, m_pageAttributes.get(),
                       idAttr_imageValue);
    select();
}

QHash<QString, QSharedPointer<QList<QImage>>> DownloadedPagesTable::imagesAt(
    const QModelIndex &index) const
{
    QHash<QString, QSharedPointer<QList<QImage>>> result;
    if (!index.isValid() || m_imageAttributeIds.isEmpty()) {
        return result;
    }

    // Qt's QSqlTableModel::record(int) retrieves BLOB values via an internal
    // scrollable QSqlQuery.  Depending on the QSQLITE driver version, that
    // path can silently convert BLOBs to empty byte arrays.  We bypass the
    // model cache entirely and issue a fresh, targeted SELECT through the
    // model's own database connection so that the QSQLITE driver delivers the
    // raw bytes directly from sqlite3_column_blob().
    const QString rowId =
        QSqlTableModel::data(index.siblingAtColumn(0), Qt::DisplayRole).toString();
    if (rowId.isEmpty()) {
        return result;
    }

    const QStringList imgCols(m_imageAttributeIds.cbegin(), m_imageAttributeIds.cend());
    const QString sql = QString("SELECT %1 FROM %2 WHERE id = ?")
                            .arg(imgCols.join(", "), AspiredDb::TABLE_NAME);

    QSqlQuery q(database());
    q.prepare(sql);
    q.addBindValue(rowId);
    q.exec();

    if (q.next()) {
        for (int i = 0; i < imgCols.size(); ++i) {
            const QByteArray blob = q.value(i).toByteArray();
            const QList<QSharedPointer<QImage>> imgPtrs = AspiredDb::deserializeImages(blob);

            auto imgList = QSharedPointer<QList<QImage>>::create();
            imgList->reserve(imgPtrs.size());
            for (const auto &imgPtr : std::as_const(imgPtrs)) {
                imgList->append(*imgPtr);
            }
            result.insert(imgCols[i], imgList);
        }
    }
    return result;
}

void DownloadedPagesTable::deleteRows(const QList<QString> &rowIds)
{
    if (rowIds.isEmpty()) {
        return;
    }

    // Build a single DELETE ... WHERE id IN (?, ?, ...) for atomicity.
    QStringList placeholders;
    placeholders.reserve(rowIds.size());
    for (int i = 0; i < rowIds.size(); ++i) {
        placeholders.append("?");
    }
    const QString sql = QString("DELETE FROM %1 WHERE id IN (%2)")
                            .arg(AspiredDb::TABLE_NAME, placeholders.join(", "));

    QSqlQuery q(database());
    q.prepare(sql);
    for (const auto &id : std::as_const(rowIds)) {
        q.addBindValue(id);
    }

    if (!q.exec()) {
        ExceptionWithTitleText ex(
            tr("Database Error"),
            tr("Failed to delete rows: %1").arg(q.lastError().text()));
        ex.raise();
    }

    select();
}

Qt::ItemFlags DownloadedPagesTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    const QString colName =
        headerData(index.column(), Qt::Horizontal, Qt::DisplayRole).toString();
    if (m_imageAttributeIds.contains(colName)) {
        return QSqlTableModel::flags(index) & ~Qt::ItemIsEditable;
    }
    return QSqlTableModel::flags(index) | Qt::ItemIsEditable;
}

bool DownloadedPagesTable::setData(const QModelIndex & /*index*/,
                                   const QVariant & /*value*/,
                                   int /*role*/)
{
    return false;
}

QVariant DownloadedPagesTable::data(const QModelIndex &index, int role) const
{
    if ((role == Qt::DisplayRole || role == Qt::EditRole) && index.isValid()) {
        const QString colName =
            headerData(index.column(), Qt::Horizontal, Qt::DisplayRole).toString();
        if (m_imageAttributeIds.contains(colName)) {
            // The model holds only the first 4 bytes of the BLOB: the
            // QDataStream big-endian qint32 image count written by
            // serializeImages().  Parse it to show "N images".
            const QByteArray header =
                QSqlTableModel::data(index, Qt::DisplayRole).toByteArray();
            if (header.size() < 4) {
                return tr("0 images");
            }
            QDataStream stream(header);
            stream.setVersion(QDataStream::Qt_6_0);
            qint32 count = 0;
            stream >> count;
            return tr("%1 images").arg(count);
        }
    }
    if (role == Qt::DisplayRole && index.isValid()) {
        // HarfBuzz glyph shaping is O(n) in text length.  Shaping thousands
        // of characters per cell (e.g. the description field) blocks the main
        // thread for seconds in a debug build.  Truncate display text to keep
        // painting fast; the full value is still stored in the database.
        const QVariant v = QSqlTableModel::data(index, role);
        const QString s = v.toString();
        if (s.length() > 120) {
            return s.left(117) + QStringLiteral("...");
        }
        return v;
    }
    return QSqlTableModel::data(index, role);
}
