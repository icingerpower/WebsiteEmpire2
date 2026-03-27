#ifndef PAGEATTRIBUTESHEALTHMENTALCONDITION_H
#define PAGEATTRIBUTESHEALTHMENTALCONDITION_H

#include "../AbstractPageAttributes.h"

// Page attributes for a mental health condition, e.g. "Depression", "Anxiety".
// Each record stores the condition name, an estimated prevalence percentage,
// and optional references to associated body parts, symptoms, organs, and injuries.
class PageAttributesHealthMentalCondition : public AbstractPageAttributes
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

#endif // PAGEATTRIBUTESHEALTHMENTALCONDITION_H
