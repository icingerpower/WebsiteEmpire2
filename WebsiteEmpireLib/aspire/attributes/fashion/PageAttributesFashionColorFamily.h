#ifndef PAGEATTRIBUTESFASHIONCOLORFAMILY_H
#define PAGEATTRIBUTESFASHIONCOLORFAMILY_H

#include "../AbstractPageAttributes.h"

// Page attributes for a color family (e.g. "Neutrals", "Pastels", "Jewel Tones").
// Used as a reference target from PageAttributesFashionColor::ID_FAMILY.
class PageAttributesFashionColorFamily : public AbstractPageAttributes
{
    Q_OBJECT

public:
    // Stable attribute ID for the family name — used as the reference target
    // in PageAttributesFashionColor::ID_FAMILY's ReferenceSpec.
    static const QString ID_NAME;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESFASHIONCOLORFAMILY_H
