#ifndef PAGEATTRIBUTESHEALTHINJURY_H
#define PAGEATTRIBUTESHEALTHINJURY_H

#include "../AbstractPageAttributes.h"

// Page attributes for a health injury type (e.g. "Fracture", "Sprain").
// Used as a reference target from health condition attribute classes.
// ID is a unique integer identifier across the health reference types.
class PageAttributesHealthInjury : public AbstractPageAttributes
{
    Q_OBJECT

public:
    static const int ID = 4;

    // Stable attribute ID for the injury name — used as the reference
    // target in health condition ReferenceSpec declarations.
    static const QString ID_INJURY_NAME;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESHEALTHINJURY_H
