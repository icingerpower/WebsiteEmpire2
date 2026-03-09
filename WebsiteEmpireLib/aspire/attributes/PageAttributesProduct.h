#ifndef PAGEATTRIBUTESPRODUCT_H
#define PAGEATTRIBUTESPRODUCT_H

#include "AbstractPageAttributes.h"

class PageAttributesProduct : public AbstractPageAttributes
{
    Q_OBJECT

public:
    static const QString ID_CATEGORY;
    static const QString ID_SALE_PRICE;
    static const QString ID_NAME;
    static const QString ID_DESCRIPTION;
    static const QString ID_SUPPLIER_PRICE;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESPRODUCT_H
