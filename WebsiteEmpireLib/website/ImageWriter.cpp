#include "ImageWriter.h"

#include <QBuffer>
#include <QDebug>
#include <QImage>
#include <QSqlDatabase>
#include <QSqlError>
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
    if (!db.open()) {
        qWarning() << "ImageWriter: failed to open" << db.databaseName()
                   << "-" << db.lastError().text();
    }
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
    if (!q.exec()) {
        qWarning() << "ImageWriter::writeImage INSERT failed:" << q.lastError().text();
        return -1;
    }
    const QVariant insertId = q.lastInsertId();
    return insertId.isValid() ? insertId.toLongLong() : -1;
}

void ImageWriter::linkName(qint64 imageId, const QString &domain, const QString &filename)
{
    // Qt SQL binds null QString as SQL NULL, which violates the NOT NULL constraint.
    // Coerce null to empty string so domain="" is stored correctly.
    const QString &safeDomain = domain.isNull() ? QStringLiteral("") : domain;

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO image_names (domain, filename, image_id)"
        " VALUES (:domain, :filename, :image_id)"));
    q.bindValue(QStringLiteral(":domain"),   safeDomain);
    q.bindValue(QStringLiteral(":filename"), filename);
    q.bindValue(QStringLiteral(":image_id"), imageId);
    if (!q.exec()) {
        qWarning() << "ImageWriter::linkName INSERT failed:" << q.lastError().text();
    }
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
    const QByteArray blob = buf.data();
    const qint64 imageId = writeImage(blob, QStringLiteral("image/webp"));
    if (imageId < 0) {
        return -1;
    }
    linkName(imageId, domain, filename);
    return imageId;
}

qint64 ImageWriter::writeSvg(const QByteArray &svgData,
                              const QString    &domain,
                              const QString    &filename)
{
    const qint64 imageId = writeImage(svgData, QStringLiteral("image/svg+xml"));
    if (imageId < 0) {
        return -1;
    }
    linkName(imageId, domain, filename);
    return imageId;
}

QByteArray ImageWriter::readSvg(const QString &domain, const QString &filename) const
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT i.blob FROM images i"
        " JOIN image_names n ON n.image_id = i.id"
        " WHERE n.domain = :domain AND n.filename = :filename"
        "   AND i.mime_type = 'image/svg+xml'"
        " LIMIT 1"));
    q.bindValue(QStringLiteral(":domain"),   domain);
    q.bindValue(QStringLiteral(":filename"), filename);
    if (!q.exec() || !q.next()) {
        return {};
    }
    return q.value(0).toByteArray();
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
