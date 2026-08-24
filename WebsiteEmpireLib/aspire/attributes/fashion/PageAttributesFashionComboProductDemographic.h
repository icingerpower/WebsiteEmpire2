#ifndef PAGEATTRIBUTESFASHIONCOMBOPRODUCTDEMOGRAPHIC_H
#define PAGEATTRIBUTESFASHIONCOMBOPRODUCTDEMOGRAPHIC_H

#include "PageAttributesFashionComboBase.h"

// Combination of ProductType + Demographic. Covers the expansion formula
// "best {productType} for {demographic}" — strong-commercial-intent queries
// targeting body proportions or age segments ("best jeans for pear shape",
// "best blazer for broad shoulders"), per the Google-Ads-volume keyword
// research pass.
class PageAttributesFashionComboProductDemographic : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_PRODUCT_TYPE;
    static const QString ID_DEMOGRAPHIC;

    static const QString FORMULA_BEST_PRODUCT_FOR_DEMOGRAPHIC;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOPRODUCTDEMOGRAPHIC_H
