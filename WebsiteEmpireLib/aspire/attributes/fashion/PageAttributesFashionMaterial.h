#ifndef PAGEATTRIBUTESFASHIONMATERIAL_H
#define PAGEATTRIBUTESFASHIONMATERIAL_H

#include "../AbstractPageAttributes.h"

// Page attributes for a fashion material/fabric (e.g. "Silk", "Cashmere",
// "Denim"). Used as a reference target from the combination attribute
// classes (PageAttributesFashionCombo*).
class PageAttributesFashionMaterial : public AbstractPageAttributes
{
    Q_OBJECT

public:
    // Stable attribute ID for the material name — used as the reference
    // target in combo ReferenceSpec declarations.
    static const QString ID_NAME;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESFASHIONMATERIAL_H
