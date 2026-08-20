#include <QObject>

#include "PageAttributesFashionColor.h"
#include "PageAttributesFashionColorFamily.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionColor);

const QString PageAttributesFashionColor::ID_NAME   = QStringLiteral("fashion_color_name");
const QString PageAttributesFashionColor::ID_FAMILY = QStringLiteral("fashion_color_family");

QString PageAttributesFashionColor::getId() const
{
    return QStringLiteral("PageAttributesFashionColor");
}

QString PageAttributesFashionColor::getName() const
{
    return QObject::tr("Fashion Color");
}

QString PageAttributesFashionColor::getDescription() const
{
    return QObject::tr("Attributes defining a granular fashion color/shade (e.g. Emerald Green)");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionColor::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_NAME
                            , tr("Name")
                            , tr("The name of the color/shade")
                            , tr("Emerald Green")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The color name can't be empty");
                                }
                                return QString{};
                            }
    };

    *attributes << Attribute{ID_FAMILY
                            , tr("Family")
                            , tr("The color family this shade belongs to")
                            , tr("Jewel Tones")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The color family can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , false // mandatory
                            , ReferenceSpec{PageAttributesFashionColorFamily::ID_NAME, ReferenceSpec::Cardinality::Single}
    };

    return attributes;
}
