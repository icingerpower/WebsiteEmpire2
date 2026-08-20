#ifndef PAGEATTRIBUTESFASHIONEVENT_H
#define PAGEATTRIBUTESFASHIONEVENT_H

#include "../AbstractPageAttributes.h"

// Page attributes for a fashion event/occasion (e.g. "Wedding Guest",
// "Gala/Black Tie"). Used as a reference target from the combination
// attribute classes (PageAttributesFashionCombo*).
class PageAttributesFashionEvent : public AbstractPageAttributes
{
    Q_OBJECT

public:
    // Stable attribute ID for the event name — used as the reference target
    // in combo ReferenceSpec declarations.
    static const QString ID_NAME;

    // Free-text formality descriptor (optional), e.g. "Black Tie", "Casual".
    static const QString ID_FORMALITY;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESFASHIONEVENT_H
