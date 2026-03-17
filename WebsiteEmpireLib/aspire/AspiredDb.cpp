#include "AspiredDb.h"

#include <QBuffer>
#include <QDataStream>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include "ExceptionWithTitleText.h"

static QByteArray serializeImages(const QList<QSharedPointer<QImage>> &images)
{
    QByteArray blob;
    QDataStream stream(&blob, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << static_cast<qint32>(images.size());
    for (const auto &img : std::as_const(images)) {
        QByteArray imgBytes;
        QBuffer buf(&imgBytes);
        buf.open(QIODevice::WriteOnly);
        img->save(&buf, "PNG");
        stream << imgBytes;
    }
    return blob;
}

QList<QSharedPointer<QImage>> AspiredDb::deserializeImages(const QByteArray &blob)
{
    QList<QSharedPointer<QImage>> images;
    if (blob.isEmpty()) {
        return images;
    }
    QDataStream stream(blob);
    stream.setVersion(QDataStream::Qt_6_0);
    qint32 count = 0;
    stream >> count;
    images.reserve(count);
    for (qint32 i = 0; i < count; ++i) {
        QByteArray imgBytes;
        stream >> imgBytes;
        auto img = QSharedPointer<QImage>::create();
        img->loadFromData(imgBytes, "PNG");
        images.append(img);
    }
    return images;
}

const QString AspiredDb::TABLE_NAME = "records";

AspiredDb::AspiredDb(const QString &workingDir, const QString &retrieverID)
    : m_connectionName(retrieverID + "_" + QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    db.setDatabaseName(workingDir + "/" + retrieverID + ".db");
    if (!db.open()) {
        QSqlDatabase::removeDatabase(m_connectionName);
        throw ExceptionWithTitleText(
            QObject::tr("Database Error"),
            QObject::tr("Failed to open database '%1': %2").arg(retrieverID, db.lastError().text())
        );
    }
}

AspiredDb::~AspiredDb()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isValid())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

void AspiredDb::createTableIdNeed(const QList<AbstractPageAttributes::Attribute> &attributes)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

    // Build CREATE TABLE with all attribute columns
    QStringList columnDefs;
    columnDefs << "id INTEGER PRIMARY KEY AUTOINCREMENT";
    for (const auto &attr : attributes) {
        const QString colType = attr.validateImageList.has_value() ? "BLOB" : "TEXT";
        columnDefs << QString("%1 %2").arg(attr.id, colType);
    }

    const QString createSql = QString("CREATE TABLE IF NOT EXISTS %1 (%2)")
                                  .arg(TABLE_NAME, columnDefs.join(", "));

    if (!query.exec(createSql)) {
        throw ExceptionWithTitleText(
            QObject::tr("Database Error"),
            QObject::tr("Failed to create table: %1").arg(query.lastError().text())
        );
    }

    // Collect existing columns
    QSet<QString> existingColumns;
    if (!query.exec(QString("PRAGMA table_info(%1)").arg(TABLE_NAME))) {
        throw ExceptionWithTitleText(
            QObject::tr("Database Error"),
            QObject::tr("Failed to read table schema: %1").arg(query.lastError().text())
        );
    }
    while (query.next())
        existingColumns.insert(query.value("name").toString());

    // Add any attribute columns that are missing
    for (const auto &attr : attributes) {
        if (existingColumns.contains(attr.id))
            continue;

        const QString colType = attr.validateImageList.has_value() ? "BLOB" : "TEXT";
        const QString alterSql = QString("ALTER TABLE %1 ADD COLUMN %2 %3")
                                     .arg(TABLE_NAME, attr.id, colType);
        if (!query.exec(alterSql)) {
            throw ExceptionWithTitleText(
                QObject::tr("Database Error"),
                QObject::tr("Failed to add column '%1': %2").arg(attr.id, query.lastError().text())
            );
        }
    }
}

void AspiredDb::record(const QList<AbstractPageAttributes::Attribute> &attributes,
                       const QHash<QString, QString> &idAttr_value,
                       const AbstractPageAttributes *pageAttributes,
                       const QHash<QString, QList<QSharedPointer<QImage>>> &idAttr_imageValue)
{
    // Validate individual attributes
    for (const auto &attr : attributes) {
        if (attr.validateImageList.has_value()) {
            const auto &images = idAttr_imageValue.value(attr.id);
            const QString error = (*attr.validateImageList)(images);
            if (!error.isEmpty()) {
                throw ExceptionWithTitleText(attr.name, error);
            }
        } else {
            const QString value = idAttr_value.value(attr.id);
            const QString error = attr.validate(value);
            if (!error.isEmpty()) {
                throw ExceptionWithTitleText(attr.name, error);
            }
        }
    }

    // Cross-validate
    const QString crossError = pageAttributes->areAttributesCrossValid(idAttr_value);
    if (!crossError.isEmpty())
        throw ExceptionWithTitleText(pageAttributes->getName(), crossError);

    // Ensure table schema is current
    createTableIdNeed(attributes);

    // Build and execute INSERT
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

    QStringList columns;
    QStringList placeholders;
    for (const auto &attr : attributes) {
        columns << attr.id;
        placeholders << ":" + attr.id;
    }

    const QString insertSql = QString("INSERT INTO %1 (%2) VALUES (%3)")
                                  .arg(TABLE_NAME, columns.join(", "), placeholders.join(", "));

    query.prepare(insertSql);
    for (const auto &attr : attributes) {
        if (attr.validateImageList.has_value()) {
            query.bindValue(":" + attr.id, serializeImages(idAttr_imageValue.value(attr.id)));
        } else {
            query.bindValue(":" + attr.id, idAttr_value.value(attr.id));
        }
    }

    if (!query.exec()) {
        throw ExceptionWithTitleText(
            QObject::tr("Database Error"),
            QObject::tr("Failed to insert record: %1").arg(query.lastError().text())
        );
    }
}
