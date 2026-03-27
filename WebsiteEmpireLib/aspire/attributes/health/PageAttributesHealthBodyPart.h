#ifndef PAGEATTRIBUTESHEALTHBODYPART_H
#define PAGEATTRIBUTESHEALTHBODYPART_H

#include "../AbstractPageAttributes.h"

// Page attributes for a health body part (e.g. "Knee", "Shoulder").
// Used as a reference target from health condition attribute classes.
// ID is a unique integer identifier across the health reference types — use
// for switch/array indexing when working with multiple health reference types.
class PageAttributesHealthBodyPart : public AbstractPageAttributes
{
    Q_OBJECT

public:
    static const int ID = 1;

    // Stable attribute ID for the body-part name — used as the reference
    // target in health condition ReferenceSpec declarations.
    static const QString ID_BODY_PART_NAME;

    // Whether this body part is located in the brain. Stored as "true"/"false".
    // Allows quick filtering of brain-specific body parts.
    static const QString ID_IS_BRAIN_PART;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESHEALTHBODYPART_H
