#include <QObject>

#include "PageAttributesHealthSymptom.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesHealthSymptom);

const QString PageAttributesHealthSymptom::ID_SYMPTOM_NAME = QStringLiteral("health_symptom_name");

QString PageAttributesHealthSymptom::getId() const
{
    return QStringLiteral("PageAttributesHealthSymptom");
}

QString PageAttributesHealthSymptom::getName() const
{
    return QObject::tr("Health Symptom");
}

QString PageAttributesHealthSymptom::getDescription() const
{
    return QObject::tr("Attributes defining a health symptom");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesHealthSymptom::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_SYMPTOM_NAME
                            , tr("Name")
                            , tr("The name of the symptom")
                            , tr("Fatigue")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The symptom name can't be empty");
                                }
                                return QString{};
                            }
    };

    return attributes;
}
