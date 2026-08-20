#include <QObject>

#include "PageAttributesFashionColorFamily.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionColorFamily);

const QString PageAttributesFashionColorFamily::ID_NAME = QStringLiteral("fashion_color_family_name");

QString PageAttributesFashionColorFamily::getId() const
{
    return QStringLiteral("PageAttributesFashionColorFamily");
}

QString PageAttributesFashionColorFamily::getName() const
{
    return QObject::tr("Fashion Color Family");
}

QString PageAttributesFashionColorFamily::getDescription() const
{
    return QObject::tr("Attributes defining a color family (e.g. Neutrals, Pastels, Jewel Tones)");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionColorFamily::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_NAME
                            , tr("Name")
                            , tr("The name of the color family")
                            , tr("Jewel Tones")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The color family name can't be empty");
                                }
                                return QString{};
                            }
    };

    return attributes;
}
