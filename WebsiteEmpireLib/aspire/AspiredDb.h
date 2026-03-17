#ifndef ASPIREDDB_H
#define ASPIREDDB_H

#include <QHash>
#include <QList>
#include <QSharedPointer>
#include <QString>

#include "attributes/AbstractPageAttributes.h"

class QImage;

class AspiredDb
{
public:
    AspiredDb(const QString &workingDir, const QString &retrieverID);
    ~AspiredDb();

    // Creates the records table if it does not exist.
    // If the table already exists but is missing columns for some attributes,
    // those columns are added (ALTER TABLE).
    void createTableIdNeed(const QList<AbstractPageAttributes::Attribute> &attributes);

    // Validates every attribute value via its validate (or validateImageList) lambda,
    // then calls pageAttributes->areAttributesCrossValid(). Throws ExceptionWithTitleText
    // on the first validation error. On success, inserts a row into the table.
    // Image-list attributes are serialized to BLOB; their values must be supplied in
    // idAttr_imageValue keyed by the same attribute id.
    void record(const QList<AbstractPageAttributes::Attribute> &attributes,
                const QHash<QString, QString> &idAttr_value,
                const AbstractPageAttributes *pageAttributes,
                const QHash<QString, QList<QSharedPointer<QImage>>> &idAttr_imageValue = {});

    // Inverse of the internal serializeImages() used by record().
    // Returns an empty list for a null/empty blob.
    static QList<QSharedPointer<QImage>> deserializeImages(const QByteArray &blob);

    static const QString TABLE_NAME;

private:
    QString m_connectionName;
};

#endif // ASPIREDDB_H
