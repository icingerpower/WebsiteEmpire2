#include "AspiredDb.h"

#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include "ExceptionWithTitleText.h"

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
    for (const auto &attr : attributes)
        columnDefs << QString("%1 TEXT").arg(attr.id);

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

        const QString alterSql = QString("ALTER TABLE %1 ADD COLUMN %2 TEXT")
                                     .arg(TABLE_NAME, attr.id);
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
                       const AbstractPageAttributes *pageAttributes)
{
    // Validate individual attributes
    for (const auto &attr : attributes) {
        const QString value = idAttr_value.value(attr.id);
        const QString error = attr.validate(value);
        if (!error.isEmpty())
            throw ExceptionWithTitleText(attr.name, error);
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
    for (const auto &attr : attributes)
        query.bindValue(":" + attr.id, idAttr_value.value(attr.id));

    if (!query.exec()) {
        throw ExceptionWithTitleText(
            QObject::tr("Database Error"),
            QObject::tr("Failed to insert record: %1").arg(query.lastError().text())
        );
    }
}
