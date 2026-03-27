#include <QObject>

#include "PageAttributesHealthOrgan.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesHealthOrgan);

const QString PageAttributesHealthOrgan::ID_ORGAN_NAME = QStringLiteral("health_organ_name");

QString PageAttributesHealthOrgan::getId() const
{
    return QStringLiteral("PageAttributesHealthOrgan");
}

QString PageAttributesHealthOrgan::getName() const
{
    return QObject::tr("Health Organ");
}

QString PageAttributesHealthOrgan::getDescription() const
{
    return QObject::tr("Attributes defining an organ relevant to health conditions");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesHealthOrgan::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_ORGAN_NAME
                            , tr("Name")
                            , tr("The name of the organ")
                            , tr("Heart")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The organ name can't be empty");
                                }
                                return QString{};
                            }
    };

    return attributes;
}
