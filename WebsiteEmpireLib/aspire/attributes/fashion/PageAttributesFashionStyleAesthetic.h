#ifndef PAGEATTRIBUTESFASHIONSTYLEAESTHETIC_H
#define PAGEATTRIBUTESFASHIONSTYLEAESTHETIC_H

#include "../AbstractPageAttributes.h"

// Page attributes for a fashion style/aesthetic (e.g. "Old Money / Quiet
// Luxury", "Y2K Retro", "Boho Chic"). Used as a reference target from the
// combination attribute classes (PageAttributesFashionCombo*).
class PageAttributesFashionStyleAesthetic : public AbstractPageAttributes
{
    Q_OBJECT

public:
    // Stable attribute ID for the style name — used as the reference target
    // in combo ReferenceSpec declarations.
    static const QString ID_NAME;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESFASHIONSTYLEAESTHETIC_H
