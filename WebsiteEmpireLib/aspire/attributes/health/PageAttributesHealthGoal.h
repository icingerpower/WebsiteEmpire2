#ifndef PAGEATTRIBUTESHEALTHGOAL_H
#define PAGEATTRIBUTESHEALTHGOAL_H

#include "../AbstractPageAttributes.h"

// Page attributes for a health or wellness goal, e.g. "Lose Weight", "Build
// Muscle", "Reduce Anxiety".  Each record stores the goal name, an achievement
// difficulty score, and optional references to the body parts primarily involved.
//
// difficulty semantics (same scale as PageAttributesHealthCondition):
//   1 = achievable without too much effort
//   2 = some people achieve it permanently but requires significant effort;
//       standard approaches often fail
//   3 = almost no one achieves it permanently; claimed successes are not
//       fully verified
class PageAttributesHealthGoal : public AbstractPageAttributes
{
    Q_OBJECT

public:
    static const QString ID_NAME;
    static const QString ID_DIFFICULTY;
    static const QString ID_BODY_PARTS;
    static const QString ID_ORGANS;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESHEALTHGOAL_H
