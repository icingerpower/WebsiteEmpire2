#ifndef PAGEATTRIBUTESFASHIONCOMBOMATERIALPRODUCTSEASON_H
#define PAGEATTRIBUTESFASHIONCOMBOMATERIALPRODUCTSEASON_H

#include "PageAttributesFashionComboBase.h"

// Combination of Material + ProductType + Season. Covers study formula #7
// ("{material} {product} outfit {season}").
class PageAttributesFashionComboMaterialProductSeason : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_MATERIAL;
    static const QString ID_PRODUCT_TYPE;
    static const QString ID_SEASON;

    static const QString FORMULA_FABRIC_WEATHER;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOMATERIALPRODUCTSEASON_H
