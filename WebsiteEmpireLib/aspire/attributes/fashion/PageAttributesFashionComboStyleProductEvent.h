#ifndef PAGEATTRIBUTESFASHIONCOMBOSTYLEPRODUCTEVENT_H
#define PAGEATTRIBUTESFASHIONCOMBOSTYLEPRODUCTEVENT_H

#include "PageAttributesFashionComboBase.h"

// Combination of StyleAesthetic + ProductType + Event. Covers the expansion
// formula "{style} {productType} for {event}" — long-tail aesthetic-specific
// item picks for an occasion ("boho dress for music festival", "old money
// blazer for wedding guest"), per the Google-Ads-volume keyword research pass.
class PageAttributesFashionComboStyleProductEvent : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_STYLE;
    static const QString ID_PRODUCT_TYPE;
    static const QString ID_EVENT;

    static const QString FORMULA_STYLE_PRODUCT_FOR_EVENT;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOSTYLEPRODUCTEVENT_H
