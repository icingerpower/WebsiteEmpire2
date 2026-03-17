#ifndef PAGEATTRIBUTESPRODUCTCATEGORY_H
#define PAGEATTRIBUTESPRODUCTCATEGORY_H

#include "AbstractPageAttributes.h"

class PageAttributesProductCategory : public AbstractPageAttributes
{
    Q_OBJECT

public:
    static const QString ID_URL;
    static const QString ID_PRODUCT_CATEGORY;
    static const QString ID_DESCRIPTION;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESPRODUCTCATEGORY_H
