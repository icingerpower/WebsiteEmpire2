#ifndef PAGEATTRIBUTESFASHIONCOMBOCOLORPRODUCTEVENT_H
#define PAGEATTRIBUTESFASHIONCOMBOCOLORPRODUCTEVENT_H

#include "PageAttributesFashionComboBase.h"

// Combination of Color + ProductType + Event. Covers study formula #4
// ("{color} {product} for {event}") — the flagship table for the
// culture-applicability feature, since color-for-event conventions (mourning
// colors, bridal colors, festival colors, ...) vary sharply across cultures.
// Used as GeneratorFashionTaxonomy::getTables()'s `primary` table.
class PageAttributesFashionComboColorProductEvent : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_COLOR;
    static const QString ID_PRODUCT_TYPE;
    static const QString ID_EVENT;

    static const QString FORMULA_DIRECT_TRANSACTIONAL;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
    QString composeArticleTopic(const QHash<QString, QString> &rowValues) const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOCOLORPRODUCTEVENT_H
