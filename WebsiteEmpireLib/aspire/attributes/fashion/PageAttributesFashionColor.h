#ifndef PAGEATTRIBUTESFASHIONCOLOR_H
#define PAGEATTRIBUTESFASHIONCOLOR_H

#include "../AbstractPageAttributes.h"

// Page attributes for a granular fashion color/shade (e.g. "Emerald Green",
// "Off-White"). Used as a reference target from the combination attribute
// classes (PageAttributesFashionCombo*).
class PageAttributesFashionColor : public AbstractPageAttributes
{
    Q_OBJECT

public:
    // Stable attribute ID for the color name — used as the reference target
    // in combo ReferenceSpec declarations.
    static const QString ID_NAME;

    // Reference to the color's family (PageAttributesFashionColorFamily::ID_NAME).
    static const QString ID_FAMILY;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESFASHIONCOLOR_H
