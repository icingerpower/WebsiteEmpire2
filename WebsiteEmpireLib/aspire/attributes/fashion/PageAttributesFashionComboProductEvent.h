#ifndef PAGEATTRIBUTESFASHIONCOMBOPRODUCTEVENT_H
#define PAGEATTRIBUTESFASHIONCOMBOPRODUCTEVENT_H

#include "PageAttributesFashionComboBase.h"

// Combination of ProductType + Event. Covers the expansion formula
// "what {productType} to wear to {event}" — item-selection queries for a
// specific occasion ("what shoes to wear to a wedding", "what jacket to wear
// to a gala"), one of the highest-volume query families of the fashion niche
// per the Google-Ads-volume keyword research pass.
class PageAttributesFashionComboProductEvent : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_PRODUCT_TYPE;
    static const QString ID_EVENT;

    static const QString FORMULA_WHAT_PRODUCT_TO_WEAR;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOPRODUCTEVENT_H
