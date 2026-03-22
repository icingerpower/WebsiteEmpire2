#include <QObject>

#include "PageAttributesFactoryCategory.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFactoryCategory);

const QString PageAttributesFactoryCategory::ID_FACTORY_CATEGORY = QStringLiteral("factory_category_name");

QString PageAttributesFactoryCategory::getId() const
{
    return QStringLiteral("PageAttributesFactoryCategory");
}

QString PageAttributesFactoryCategory::getName() const
{
    return QObject::tr("Factory Category");
}

QString PageAttributesFactoryCategory::getDescription() const
{
    return QObject::tr("Attributes defining a manufacturing factory category");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFactoryCategory::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_FACTORY_CATEGORY
                            , tr("Name")
                            , tr("The name of the factory category")
                            , tr("Aerospace")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The category name can't be empty");
                                }
                                return QString{};
                            }
    };

    return attributes;
}
