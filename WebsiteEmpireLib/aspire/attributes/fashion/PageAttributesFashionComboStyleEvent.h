#ifndef PAGEATTRIBUTESFASHIONCOMBOSTYLEEVENT_H
#define PAGEATTRIBUTESFASHIONCOMBOSTYLEEVENT_H

#include "PageAttributesFashionComboBase.h"

// Combination of StyleAesthetic + Event. Covers the expansion formula
// "{style} {event} outfit ideas" — explicit aesthetic themes for occasions
// ("clean girl date night outfit ideas", "boho wedding guest outfits"), per
// the Google-Ads-volume keyword research pass.
class PageAttributesFashionComboStyleEvent : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_STYLE;
    static const QString ID_EVENT;

    static const QString FORMULA_STYLE_EVENT_OUTFITS;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
    QString composeArticleTopic(const QHash<QString, QString> &rowValues) const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOSTYLEEVENT_H
