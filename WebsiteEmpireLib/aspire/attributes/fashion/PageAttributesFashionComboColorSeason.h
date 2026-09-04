#ifndef PAGEATTRIBUTESFASHIONCOMBOCOLORSEASON_H
#define PAGEATTRIBUTESFASHIONCOMBOCOLORSEASON_H

#include "PageAttributesFashionComboBase.h"

// Combination of Color + Season. Covers the expansion formula
// "best {color} outfits for {season}" — seasonal color-trend queries that
// spike during wardrobe-transition periods ("best sage green outfits for
// fall"), per the Google-Ads-volume keyword research pass.
class PageAttributesFashionComboColorSeason : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_COLOR;
    static const QString ID_SEASON;

    static const QString FORMULA_COLOR_SEASON_FASHION;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
    QString composeArticleTopic(const QHash<QString, QString> &rowValues) const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOCOLORSEASON_H
