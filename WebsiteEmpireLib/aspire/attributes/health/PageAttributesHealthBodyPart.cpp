#include <QObject>

#include "PageAttributesHealthBodyPart.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesHealthBodyPart);

const QString PageAttributesHealthBodyPart::ID_BODY_PART_NAME  = QStringLiteral("health_body_part_name");
const QString PageAttributesHealthBodyPart::ID_IS_BRAIN_PART   = QStringLiteral("health_body_part_is_brain_part");

QString PageAttributesHealthBodyPart::getId() const
{
    return QStringLiteral("PageAttributesHealthBodyPart");
}

QString PageAttributesHealthBodyPart::getName() const
{
    return QObject::tr("Health Body Part");
}

QString PageAttributesHealthBodyPart::getDescription() const
{
    return QObject::tr("Attributes defining a body part relevant to health conditions");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesHealthBodyPart::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_BODY_PART_NAME
                            , tr("Name")
                            , tr("The name of the body part")
                            , tr("Knee")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The body part name can't be empty");
                                }
                                return QString{};
                            }
    };

    *attributes << Attribute{ID_IS_BRAIN_PART
                            , tr("Is Brain Part")
                            , tr("Whether this body part is located in the brain")
                            , tr("false")
                            , QStringLiteral("false")
                            , [](const QString &value) {
                                if (value != QLatin1String("true") && value != QLatin1String("false")) {
                                    return tr("Must be \"true\" or \"false\"");
                                }
                                return QString{};
                            }
    };

    return attributes;
}
