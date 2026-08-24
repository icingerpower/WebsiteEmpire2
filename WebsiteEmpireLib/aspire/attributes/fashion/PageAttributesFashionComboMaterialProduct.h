#ifndef PAGEATTRIBUTESFASHIONCOMBOMATERIALPRODUCT_H
#define PAGEATTRIBUTESFASHIONCOMBOMATERIALPRODUCT_H

#include "PageAttributesFashionComboBase.h"

// Combination of Material + ProductType. Covers the expansion formula
// "how to style a {material} {productType}" — fabric-focused styling advice
// ("how to style a leather shirt", "how to style a silk skirt"), per the
// Google-Ads-volume keyword research pass.
class PageAttributesFashionComboMaterialProduct : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_MATERIAL;
    static const QString ID_PRODUCT_TYPE;

    static const QString FORMULA_HOW_TO_STYLE_MATERIAL_PRODUCT;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOMATERIALPRODUCT_H
