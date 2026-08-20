#ifndef PAGEATTRIBUTESFASHIONCOMBOCOLORCOLOR_H
#define PAGEATTRIBUTESFASHIONCOMBOCOLORCOLOR_H

#include "PageAttributesFashionComboBase.h"

// Combination of Color + Color (an unordered pair). Covers study formula
// #10 ("{color} and {color} outfit combination").
class PageAttributesFashionComboColorColor : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_COLOR_A;
    static const QString ID_COLOR_B;

    static const QString FORMULA_COLOR_PAIRING;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;

    // Rejects colorA == colorB — "X and X outfit combination" is not a
    // meaningful color-pairing query.
    QString areAttributesCrossValid(const QHash<QString, QString> &id_values) const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOCOLORCOLOR_H
