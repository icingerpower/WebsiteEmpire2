#ifndef PAGEATTRIBUTESPRODUCTPETFOOD_H
#define PAGEATTRIBUTESPRODUCTPETFOOD_H

#include "PageAttributesProduct.h"

class PageAttributesProductPetFood : public PageAttributesProduct
{
    Q_OBJECT

public:
    static const QString ID_WEIGHT_GR;
    static const QString ID_PRICES;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;

    QString areAttributesCrossValid(const QHash<QString, QString> &id_values) const override;
};

#endif // PAGEATTRIBUTESPRODUCTPETFOOD_H
