#include <QObject>

#include "PageAttributesHealthCondition.h"
#include "PageAttributesHealthBodyPart.h"
#include "PageAttributesHealthSymptom.h"
#include "PageAttributesHealthOrgan.h"
#include "PageAttributesHealthInjury.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesHealthCondition);

const QString PageAttributesHealthCondition::ID_NAME                = QStringLiteral("health_condition_name");
const QString PageAttributesHealthCondition::ID_POPULATION_PERCENTAGE = QStringLiteral("health_condition_population_percentage");
const QString PageAttributesHealthCondition::ID_BODY_PARTS          = QStringLiteral("health_condition_body_parts");
const QString PageAttributesHealthCondition::ID_SYMPTOMS            = QStringLiteral("health_condition_symptoms");
const QString PageAttributesHealthCondition::ID_ORGANS              = QStringLiteral("health_condition_organs");
const QString PageAttributesHealthCondition::ID_INJURIES            = QStringLiteral("health_condition_injuries");

QString PageAttributesHealthCondition::getId() const
{
    return QStringLiteral("PageAttributesHealthCondition");
}

QString PageAttributesHealthCondition::getName() const
{
    return QObject::tr("Health Condition");
}

QString PageAttributesHealthCondition::getDescription() const
{
    return QObject::tr("Attributes defining a physical (non-mental) health condition");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesHealthCondition::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_NAME
                            , tr("Name")
                            , tr("The name of the health condition")
                            , tr("Hypertension")
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
                            , tr("10.0")
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

    *attributes << Attribute{ID_BODY_PARTS
                            , tr("Body Parts")
                            , tr("Body parts associated with this condition")
                            , tr("Knee")
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
                            , tr("Heart")
                            , QString{}
                            , [](const QString &) { return QString{}; }
                            , std::nullopt
                            , true // Optional
                            , ReferenceSpec{PageAttributesHealthOrgan::ID_ORGAN_NAME, ReferenceSpec::Cardinality::Multiple}
    };

    *attributes << Attribute{ID_INJURIES
                            , tr("Injuries")
                            , tr("Types of injury associated with this condition")
                            , tr("Fracture")
                            , QString{}
                            , [](const QString &) { return QString{}; }
                            , std::nullopt
                            , true // Optional
                            , ReferenceSpec{PageAttributesHealthInjury::ID_INJURY_NAME, ReferenceSpec::Cardinality::Multiple}
    };

    return attributes;
}
