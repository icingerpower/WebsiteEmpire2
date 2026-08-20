#include <QObject>

#include "PageAttributesFashionDemographic.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionDemographic);

const QString PageAttributesFashionDemographic::ID_NAME = QStringLiteral("fashion_demographic_name");

QString PageAttributesFashionDemographic::getId() const
{
    return QStringLiteral("PageAttributesFashionDemographic");
}

QString PageAttributesFashionDemographic::getName() const
{
    return QObject::tr("Fashion Demographic/Body Type");
}

QString PageAttributesFashionDemographic::getDescription() const
{
    return QObject::tr("Attributes defining a target demographic or body type (e.g. Petite, Hourglass, Maternity)");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionDemographic::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_NAME
                            , tr("Name")
                            , tr("The name of the demographic/body type")
                            , tr("Hourglass")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The demographic name can't be empty");
                                }
                                return QString{};
                            }
    };

    return attributes;
}
