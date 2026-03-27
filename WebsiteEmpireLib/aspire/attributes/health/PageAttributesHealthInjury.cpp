#include <QObject>

#include "PageAttributesHealthInjury.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesHealthInjury);

const QString PageAttributesHealthInjury::ID_INJURY_NAME = QStringLiteral("health_injury_name");

QString PageAttributesHealthInjury::getId() const
{
    return QStringLiteral("PageAttributesHealthInjury");
}

QString PageAttributesHealthInjury::getName() const
{
    return QObject::tr("Health Injury");
}

QString PageAttributesHealthInjury::getDescription() const
{
    return QObject::tr("Attributes defining a type of injury relevant to health conditions");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesHealthInjury::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_INJURY_NAME
                            , tr("Name")
                            , tr("The name of the injury type")
                            , tr("Fracture")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The injury name can't be empty");
                                }
                                return QString{};
                            }
    };

    return attributes;
}
