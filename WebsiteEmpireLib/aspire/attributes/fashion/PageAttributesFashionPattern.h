#ifndef PAGEATTRIBUTESFASHIONPATTERN_H
#define PAGEATTRIBUTESFASHIONPATTERN_H

#include "../AbstractPageAttributes.h"

// Page attributes for a fashion print/pattern (e.g. "Floral", "Leopard
// Print", "Houndstooth"). Used as a reference target from the combination
// attribute classes (PageAttributesFashionCombo*).
class PageAttributesFashionPattern : public AbstractPageAttributes
{
    Q_OBJECT

public:
    // Stable attribute ID for the pattern name — used as the reference
    // target in combo ReferenceSpec declarations.
    static const QString ID_NAME;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESFASHIONPATTERN_H
