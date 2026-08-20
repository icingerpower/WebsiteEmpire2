#include <QObject>

#include "PageAttributesFashionMaterial.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionMaterial);

const QString PageAttributesFashionMaterial::ID_NAME = QStringLiteral("fashion_material_name");

QString PageAttributesFashionMaterial::getId() const
{
    return QStringLiteral("PageAttributesFashionMaterial");
}

QString PageAttributesFashionMaterial::getName() const
{
    return QObject::tr("Fashion Material");
}

QString PageAttributesFashionMaterial::getDescription() const
{
    return QObject::tr("Attributes defining a fashion material/fabric (e.g. Silk, Cashmere, Denim)");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionMaterial::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_NAME
                            , tr("Name")
                            , tr("The name of the material/fabric")
                            , tr("Cashmere")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The material name can't be empty");
                                }
                                return QString{};
                            }
    };

    return attributes;
}
