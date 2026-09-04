#ifndef PAGEATTRIBUTESFASHIONCOMBOFITPRODUCTEVENT_H
#define PAGEATTRIBUTESFASHIONCOMBOFITPRODUCTEVENT_H

#include "PageAttributesFashionComboBase.h"

// Combination of Fit/Silhouette + ProductType + Event. Covers study formula
// #8 ("{fit/cut} {product} for {event}").
class PageAttributesFashionComboFitProductEvent : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_FIT;
    static const QString ID_PRODUCT_TYPE;
    static const QString ID_EVENT;

    static const QString FORMULA_SILHOUETTE_OCCASION;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
    QString composeArticleTopic(const QHash<QString, QString> &rowValues) const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOFITPRODUCTEVENT_H
