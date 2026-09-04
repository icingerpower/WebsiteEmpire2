#ifndef PAGEATTRIBUTESFASHIONCOMBOSEASONEVENT_H
#define PAGEATTRIBUTESFASHIONCOMBOSEASONEVENT_H

#include "PageAttributesFashionComboBase.h"

// Combination of Season + Event. Covers study formula #2 ("{season} {event}
// outfit ideas").
class PageAttributesFashionComboSeasonEvent : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_SEASON;
    static const QString ID_EVENT;

    static const QString FORMULA_OCCASION_SEASONALITY;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
    QString composeArticleTopic(const QHash<QString, QString> &rowValues) const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOSEASONEVENT_H
