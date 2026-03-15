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
    static const QString ID_IMAGES;

    // Smallest side (width or height) each product image must have, in pixels.
    static const int MIN_IMAGE_SIDE_PX = 200;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESPRODUCT_H
