#ifndef PAGEATTRIBUTESHEALTHORGAN_H
#define PAGEATTRIBUTESHEALTHORGAN_H

#include "../AbstractPageAttributes.h"

// Page attributes for a health organ (e.g. "Heart", "Liver").
// Used as a reference target from health condition attribute classes.
// ID is a unique integer identifier across the health reference types.
class PageAttributesHealthOrgan : public AbstractPageAttributes
{
    Q_OBJECT

public:
    static const int ID = 3;

    // Stable attribute ID for the organ name — used as the reference
    // target in health condition ReferenceSpec declarations.
    static const QString ID_ORGAN_NAME;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESHEALTHORGAN_H
