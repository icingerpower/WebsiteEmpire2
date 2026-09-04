#ifndef PAGEATTRIBUTESFASHIONCOMBOSTYLEPRODUCT_H
#define PAGEATTRIBUTESFASHIONCOMBOSTYLEPRODUCT_H

#include "PageAttributesFashionComboBase.h"

// Combination of StyleAesthetic + ProductType. Covers the expansion formula
// "{style} {productType} outfits" — aesthetic-specific executions of a single
// garment type ("old money blazer outfit", "streetwear hoodie outfit"),
// identified as a high-aggregate-volume query family by the Google-Ads-volume
// keyword research pass.
class PageAttributesFashionComboStyleProduct : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_STYLE;
    static const QString ID_PRODUCT_TYPE;

    static const QString FORMULA_STYLE_PRODUCT_OUTFITS;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
    QString composeArticleTopic(const QHash<QString, QString> &rowValues) const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOSTYLEPRODUCT_H
