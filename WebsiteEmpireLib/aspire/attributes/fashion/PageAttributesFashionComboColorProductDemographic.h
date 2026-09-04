#ifndef PAGEATTRIBUTESFASHIONCOMBOCOLORPRODUCTDEMOGRAPHIC_H
#define PAGEATTRIBUTESFASHIONCOMBOCOLORPRODUCTDEMOGRAPHIC_H

#include "PageAttributesFashionComboBase.h"

// Combination of Color + ProductType + Demographic. Covers the expansion
// formula "{demographic} {color} {productType}" — very-high-volume
// commercial queries like "plus size black dress" or "petite white jeans",
// per the Google-Ads-volume keyword research pass.
class PageAttributesFashionComboColorProductDemographic : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_COLOR;
    static const QString ID_PRODUCT_TYPE;
    static const QString ID_DEMOGRAPHIC;

    static const QString FORMULA_COLOR_PRODUCT_FOR_DEMOGRAPHIC;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
    QString composeArticleTopic(const QHash<QString, QString> &rowValues) const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOCOLORPRODUCTDEMOGRAPHIC_H
