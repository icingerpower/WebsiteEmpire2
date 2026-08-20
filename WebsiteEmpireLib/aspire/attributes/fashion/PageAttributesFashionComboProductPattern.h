#ifndef PAGEATTRIBUTESFASHIONCOMBOPRODUCTPATTERN_H
#define PAGEATTRIBUTESFASHIONCOMBOPRODUCTPATTERN_H

#include "PageAttributesFashionComboBase.h"

// Combination of ProductType + Pattern. Covers study formula #12
// ("outfit with {pattern} {product}").
class PageAttributesFashionComboProductPattern : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_PRODUCT_TYPE;
    static const QString ID_PATTERN;

    static const QString FORMULA_PRINT_PATTERN_STYLING;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOPRODUCTPATTERN_H
