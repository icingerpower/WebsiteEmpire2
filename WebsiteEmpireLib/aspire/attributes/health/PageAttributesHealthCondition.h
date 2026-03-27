#ifndef PAGEATTRIBUTESHEALTHCONDITION_H
#define PAGEATTRIBUTESHEALTHCONDITION_H

#include "../AbstractPageAttributes.h"

// Page attributes for a physical health condition (non-mental), e.g. "Diabetes",
// "Hypertension".  Each record stores the condition name, an estimated prevalence
// percentage, and optional references to associated body parts, symptoms, organs,
// and injuries.
class PageAttributesHealthCondition : public AbstractPageAttributes
{
    Q_OBJECT

public:
    static const QString ID_NAME;
    static const QString ID_POPULATION_PERCENTAGE;
    static const QString ID_BODY_PARTS;
    static const QString ID_SYMPTOMS;
    static const QString ID_ORGANS;
    static const QString ID_INJURIES;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESHEALTHCONDITION_H
