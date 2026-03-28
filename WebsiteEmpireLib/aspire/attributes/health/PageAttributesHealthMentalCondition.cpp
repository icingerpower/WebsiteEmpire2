#include <QObject>

#include "PageAttributesHealthMentalCondition.h"
#include "PageAttributesHealthBodyPart.h"
#include "PageAttributesHealthSymptom.h"
#include "PageAttributesHealthOrgan.h"
#include "PageAttributesHealthInjury.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesHealthMentalCondition);

const QString PageAttributesHealthMentalCondition::ID_NAME                = QStringLiteral("health_mental_condition_name");
const QString PageAttributesHealthMentalCondition::ID_POPULATION_PERCENTAGE = QStringLiteral("health_mental_condition_population_percentage");
const QString PageAttributesHealthMentalCondition::ID_HEALING_DIFFICULTY  = QStringLiteral("health_mental_condition_healing_difficulty");
const QString PageAttributesHealthMentalCondition::ID_BODY_PARTS          = QStringLiteral("health_mental_condition_body_parts");
const QString PageAttributesHealthMentalCondition::ID_SYMPTOMS            = QStringLiteral("health_mental_condition_symptoms");
const QString PageAttributesHealthMentalCondition::ID_ORGANS              = QStringLiteral("health_mental_condition_organs");
const QString PageAttributesHealthMentalCondition::ID_INJURIES            = QStringLiteral("health_mental_condition_injuries");

QString PageAttributesHealthMentalCondition::getId() const
{
    return QStringLiteral("PageAttributesHealthMentalCondition");
}

QString PageAttributesHealthMentalCondition::getName() const
{
    return QObject::tr("Mental Health Condition");
}

QString PageAttributesHealthMentalCondition::getDescription() const
{
    return QObject::tr("Attributes defining a mental health condition");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesHealthMentalCondition::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_NAME
                            , tr("Name")
                            , tr("The name of the mental health condition")
                            , tr("Depression")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The condition name can't be empty");
                                }
                                return QString{};
                            }
    };

    *attributes << Attribute{ID_POPULATION_PERCENTAGE
                            , tr("Estimated Population Percentage")
                            , tr("Estimated percentage of the global population affected by this condition")
                            , tr("5.0")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The population percentage can't be empty");
                                }
                                bool ok;
                                const double percentage = value.toDouble(&ok);
                                if (!ok) {
                                    return tr("The population percentage must be a number");
                                }
                                if (percentage < 0.0 || percentage > 100.0) {
                                    return tr("The population percentage must be between 0 and 100");
                                }
                                return QString{};
                            }
    };

    *attributes << Attribute{ID_HEALING_DIFFICULTY
                            , tr("Healing Difficulty")
                            , tr("How difficult the condition is to permanently resolve: "
                                 "1 = can be resolved without too much effort; "
                                 "2 = some people resolve it permanently but requires significant "
                                 "effort, and normal medicine often fails; "
                                 "3 = almost no one resolves it permanently, and claimed recoveries "
                                 "are not fully verified")
                            , tr("2")
                            , QString{}
                            , [](const QString &value) {
                                if (value != QLatin1String("1")
                                    && value != QLatin1String("2")
                                    && value != QLatin1String("3")) {
                                    return tr("Healing difficulty must be 1, 2, or 3");
                                }
                                return QString{};
                            }
    };

    *attributes << Attribute{ID_BODY_PARTS
                            , tr("Body Parts")
                            , tr("Body parts associated with this condition")
                            , tr("Brain")
                            , QString{}
                            , [](const QString &) { return QString{}; }
                            , std::nullopt
                            , true // Optional
                            , ReferenceSpec{PageAttributesHealthBodyPart::ID_BODY_PART_NAME, ReferenceSpec::Cardinality::Multiple}
    };

    *attributes << Attribute{ID_SYMPTOMS
                            , tr("Symptoms")
                            , tr("Symptoms associated with this condition")
                            , tr("Fatigue")
                            , QString{}
                            , [](const QString &) { return QString{}; }
                            , std::nullopt
                            , true // Optional
                            , ReferenceSpec{PageAttributesHealthSymptom::ID_SYMPTOM_NAME, ReferenceSpec::Cardinality::Multiple}
    };

    *attributes << Attribute{ID_ORGANS
                            , tr("Organs")
                            , tr("Organs affected by this condition")
                            , tr("Brain")
                            , QString{}
                            , [](const QString &) { return QString{}; }
                            , std::nullopt
                            , true // Optional
                            , ReferenceSpec{PageAttributesHealthOrgan::ID_ORGAN_NAME, ReferenceSpec::Cardinality::Multiple}
    };

    *attributes << Attribute{ID_INJURIES
                            , tr("Injuries")
                            , tr("Types of injury associated with this condition")
                            , tr("Trauma")
                            , QString{}
                            , [](const QString &) { return QString{}; }
                            , std::nullopt
                            , true // Optional
                            , ReferenceSpec{PageAttributesHealthInjury::ID_INJURY_NAME, ReferenceSpec::Cardinality::Multiple}
    };

    return attributes;
}
