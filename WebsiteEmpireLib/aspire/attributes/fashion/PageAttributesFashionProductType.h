#ifndef PAGEATTRIBUTESFASHIONPRODUCTTYPE_H
#define PAGEATTRIBUTESFASHIONPRODUCTTYPE_H

#include "../AbstractPageAttributes.h"

// Page attributes for a fashion product type (e.g. "Midi Dress", "Trench Coat").
// Used as a reference target from the combination attribute classes
// (PageAttributesFashionCombo*).
class PageAttributesFashionProductType : public AbstractPageAttributes
{
    Q_OBJECT

public:
    // Stable attribute ID for the product-type name — used as the reference
    // target in combo ReferenceSpec declarations.
    static const QString ID_NAME;

    // Broad merchandising category (e.g. "Dresses", "Outerwear", "Bottoms",
    // "Bags", "Shoes", "Tops"). Free text, optional.
    static const QString ID_CATEGORY;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESFASHIONPRODUCTTYPE_H
