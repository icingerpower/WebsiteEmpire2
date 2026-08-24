#ifndef PAGEATTRIBUTESFASHIONCOMBOPATTERNPRODUCTSEASON_H
#define PAGEATTRIBUTESFASHIONCOMBOPATTERNPRODUCTSEASON_H

#include "PageAttributesFashionComboBase.h"

// Combination of Pattern + ProductType + Season. Covers the expansion
// formula "{pattern} {productType} for {season}" — seasonal print queries
// like "floral dress for summer" or "plaid skirt for fall", per the
// Google-Ads-volume keyword research pass.
class PageAttributesFashionComboPatternProductSeason : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_PATTERN;
    static const QString ID_PRODUCT_TYPE;
    static const QString ID_SEASON;

    static const QString FORMULA_PATTERN_PRODUCT_FOR_SEASON;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOPATTERNPRODUCTSEASON_H
