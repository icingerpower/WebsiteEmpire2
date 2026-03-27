#ifndef PAGEATTRIBUTESHEALTHSYMPTOM_H
#define PAGEATTRIBUTESHEALTHSYMPTOM_H

#include "../AbstractPageAttributes.h"

// Page attributes for a health symptom (e.g. "Fatigue", "Chest pain").
// Used as a reference target from health condition attribute classes.
// ID is a unique integer identifier across the health reference types.
class PageAttributesHealthSymptom : public AbstractPageAttributes
{
    Q_OBJECT

public:
    static const int ID = 2;

    // Stable attribute ID for the symptom name — used as the reference
    // target in health condition ReferenceSpec declarations.
    static const QString ID_SYMPTOM_NAME;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESHEALTHSYMPTOM_H
