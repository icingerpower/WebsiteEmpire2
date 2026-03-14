#ifndef ASPIREDDB_H
#define ASPIREDDB_H

#include <QHash>
#include <QList>
#include <QString>

#include "attributes/AbstractPageAttributes.h"

class AspiredDb
{
public:
    AspiredDb(const QString &workingDir, const QString &retrieverID);
    ~AspiredDb();

    // Creates the records table if it does not exist.
    // If the table already exists but is missing columns for some attributes,
    // those columns are added (ALTER TABLE).
    void createTableIdNeed(const QList<AbstractPageAttributes::Attribute> &attributes);

    // Validates every attribute value via its validate lambda, then calls
    // pageAttributes->areAttributesCrossValid(). Throws ExceptionWithTitleText
    // on the first validation error. On success, inserts a row into the table.
    void record(const QList<AbstractPageAttributes::Attribute> &attributes,
                const QHash<QString, QString> &idAttr_value,
                const AbstractPageAttributes *pageAttributes);

    static const QString TABLE_NAME;

private:
    QString m_connectionName;
};

#endif // ASPIREDDB_H
