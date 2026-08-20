#ifndef PAGEATTRIBUTESFASHIONSEASON_H
#define PAGEATTRIBUTESFASHIONSEASON_H

#include "../AbstractPageAttributes.h"

// Page attributes for a fashion season / micro-season (e.g. "Summer",
// "Resort/Cruise", "Heatwave"). Used as a reference target from the
// combination attribute classes (PageAttributesFashionCombo*).
class PageAttributesFashionSeason : public AbstractPageAttributes
{
    Q_OBJECT

public:
    // Stable attribute ID for the season name — used as the reference target
    // in combo ReferenceSpec declarations.
    static const QString ID_NAME;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESFASHIONSEASON_H
