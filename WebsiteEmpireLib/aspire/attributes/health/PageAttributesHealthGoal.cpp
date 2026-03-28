#include <QObject>

#include "PageAttributesHealthGoal.h"
#include "PageAttributesHealthBodyPart.h"
#include "PageAttributesHealthOrgan.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesHealthGoal);

const QString PageAttributesHealthGoal::ID_NAME        = QStringLiteral("health_goal_name");
const QString PageAttributesHealthGoal::ID_DIFFICULTY  = QStringLiteral("health_goal_difficulty");
const QString PageAttributesHealthGoal::ID_BODY_PARTS  = QStringLiteral("health_goal_body_parts");
const QString PageAttributesHealthGoal::ID_ORGANS      = QStringLiteral("health_goal_organs");

QString PageAttributesHealthGoal::getId() const
{
    return QStringLiteral("PageAttributesHealthGoal");
}

QString PageAttributesHealthGoal::getName() const
{
    return QObject::tr("Health Goal");
}

QString PageAttributesHealthGoal::getDescription() const
{
    return QObject::tr("Attributes defining a health or wellness goal");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesHealthGoal::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_NAME
                            , tr("Name")
                            , tr("The name of the health goal")
                            , tr("Lose Weight")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The goal name can't be empty");
                                }
                                return QString{};
                            }
    };

    *attributes << Attribute{ID_DIFFICULTY
                            , tr("Achievement Difficulty")
                            , tr("How difficult the goal is to achieve permanently: "
                                 "1 = achievable without too much effort; "
                                 "2 = some people achieve it permanently but requires "
                                 "significant effort, and standard approaches often fail; "
                                 "3 = almost no one achieves it permanently, and claimed "
                                 "successes are not fully verified")
                            , tr("2")
                            , QString{}
                            , [](const QString &value) {
                                if (value != QLatin1String("1")
                                    && value != QLatin1String("2")
                                    && value != QLatin1String("3")) {
                                    return tr("Achievement difficulty must be 1, 2, or 3");
                                }
                                return QString{};
                            }
    };

    *attributes << Attribute{ID_BODY_PARTS
                            , tr("Body Parts")
                            , tr("Body parts primarily involved in this goal")
                            , tr("Abdomen")
                            , QString{}
                            , [](const QString &) { return QString{}; }
                            , std::nullopt
                            , true // Optional
                            , ReferenceSpec{PageAttributesHealthBodyPart::ID_BODY_PART_NAME,
                                           ReferenceSpec::Cardinality::Multiple}
    };

    *attributes << Attribute{ID_ORGANS
                            , tr("Organs")
                            , tr("Organs whose proper function is key to achieving this goal")
                            , tr("Liver")
                            , QString{}
                            , [](const QString &) { return QString{}; }
                            , std::nullopt
                            , true // Optional
                            , ReferenceSpec{PageAttributesHealthOrgan::ID_ORGAN_NAME,
                                           ReferenceSpec::Cardinality::Multiple}
    };

    return attributes;
}
