#ifndef PAGEATTRIBUTESFASHIONCOMBOSTYLESEASON_H
#define PAGEATTRIBUTESFASHIONCOMBOSTYLESEASON_H

#include "PageAttributesFashionComboBase.h"

// Combination of StyleAesthetic + Season. Covers study formula #9
// ("{aesthetic} {season} outfit ideas") and #11 ("capsule wardrobe {season}
// {style}") — identical slot signature, distinguished via ID_FORMULA_ID.
class PageAttributesFashionComboStyleSeason : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_STYLE;
    static const QString ID_SEASON;

    static const QString FORMULA_MICROTREND_LIFESTYLE;
    static const QString FORMULA_CAPSULE_CURATION;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
    QString composeArticleTopic(const QHash<QString, QString> &rowValues) const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOSTYLESEASON_H
