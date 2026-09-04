#ifndef PAGEATTRIBUTESFASHIONCOMBOFITPRODUCTDEMOGRAPHIC_H
#define PAGEATTRIBUTESFASHIONCOMBOFITPRODUCTDEMOGRAPHIC_H

#include "PageAttributesFashionComboBase.h"

// Combination of Fit/Silhouette + ProductType + Demographic. Covers study
// formula #3 ("best {fit} {product} for {body shape/target}").
class PageAttributesFashionComboFitProductDemographic : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_FIT;
    static const QString ID_PRODUCT_TYPE;
    static const QString ID_DEMOGRAPHIC;

    static const QString FORMULA_FIT_RECOMMENDATION;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
    QString composeArticleTopic(const QHash<QString, QString> &rowValues) const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOFITPRODUCTDEMOGRAPHIC_H
