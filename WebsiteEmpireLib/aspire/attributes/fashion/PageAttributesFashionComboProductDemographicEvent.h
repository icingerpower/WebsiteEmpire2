#ifndef PAGEATTRIBUTESFASHIONCOMBOPRODUCTDEMOGRAPHICEVENT_H
#define PAGEATTRIBUTESFASHIONCOMBOPRODUCTDEMOGRAPHICEVENT_H

#include "PageAttributesFashionComboBase.h"

// Combination of ProductType + Demographic + Event. Covers the expansion
// formula "{demographic} {productType} for {event}" — long-tail commercial
// queries like "plus size wedding guest dress" or "maternity dress for baby
// shower", per the Google-Ads-volume keyword research pass.
class PageAttributesFashionComboProductDemographicEvent : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_PRODUCT_TYPE;
    static const QString ID_DEMOGRAPHIC;
    static const QString ID_EVENT;

    static const QString FORMULA_PRODUCT_DEMOGRAPHIC_FOR_EVENT;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOPRODUCTDEMOGRAPHICEVENT_H
