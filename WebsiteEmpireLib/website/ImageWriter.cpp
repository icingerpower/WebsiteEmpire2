#include "ImageWriter.h"

#include <QBuffer>
#include <QImage>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <atomic>

static std::atomic<int> s_counter{0};

// =============================================================================
// Constructor / Destructor
// =============================================================================

ImageWriter::ImageWriter(const QDir &workingDir)
    : m_connName(QStringLiteral("image_writer_")
                 + QString::number(s_counter.fetch_add(1)))
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
    db.setDatabaseName(workingDir.filePath(QLatin1StringView(FILENAME)));
    db.open();
    _ensureSchema();
}

ImageWriter::~ImageWriter()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connName);
        db.close();
    }
    QSqlDatabase::removeDatabase(m_connName);
}

// =============================================================================
// Public API
// =============================================================================

qint64 ImageWriter::writeImage(const QByteArray &blob, const QString &mimeType)
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO images (blob, mime_type) VALUES (:blob, :mime_type)"));
    q.bindValue(QStringLiteral(":blob"),      blob);
    q.bindValue(QStringLiteral(":mime_type"), mimeType);
    q.exec();
    const QVariant insertId = q.lastInsertId();
    return insertId.toLongLong();
}

void ImageWriter::linkName(qint64 imageId, const QString &domain, const QString &filename)
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO image_names (domain, filename, image_id)"
        " VALUES (:domain, :filename, :image_id)"
        " ON CONFLICT(domain, filename) DO UPDATE SET image_id = excluded.image_id"));
    q.bindValue(QStringLiteral(":domain"),   domain);
    q.bindValue(QStringLiteral(":filename"), filename);
    q.bindValue(QStringLiteral(":image_id"), imageId);
    q.exec();
}

qint64 ImageWriter::writeQImage(const QImage  &image,
                                const QString &domain,
                                const QString &filename)
{
    QBuffer buf;
    buf.open(QBuffer::WriteOnly);
    if (!image.save(&buf, "webp")) {
        qWarning("ImageWriter::writeQImage: WebP encoding failed — "
                 "is the Qt WebP image plugin installed?");
        return -1;
    }
    const QByteArray &blob = buf.data();

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    db.transaction();
    const qint64 imageId = writeImage(blob, QStringLiteral("image/webp"));
    linkName(imageId, domain, filename);
    db.commit();
    return imageId;
}

qint64 ImageWriter::writeSvg(const QByteArray &svgData,
                              const QString    &domain,
                              const QString    &filename)
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    db.transaction();
    const qint64 imageId = writeImage(svgData, QStringLiteral("image/svg+xml"));
    linkName(imageId, domain, filename);
    db.commit();
    return imageId;
}

// =============================================================================
// Private
// =============================================================================

void ImageWriter::_ensureSchema()
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS images ("
        "  id        INTEGER PRIMARY KEY,"
        "  blob      BLOB    NOT NULL,"
        "  mime_type TEXT    NOT NULL"
        ")"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS image_names ("
        "  domain   TEXT    NOT NULL,"
        "  filename TEXT    NOT NULL,"
        "  image_id INTEGER NOT NULL REFERENCES images(id),"
        "  PRIMARY KEY (domain, filename)"
        ")"));
}
