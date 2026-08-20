#include <QObject>

#include "PageAttributesFashionStyleAesthetic.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionStyleAesthetic);

const QString PageAttributesFashionStyleAesthetic::ID_NAME = QStringLiteral("fashion_style_aesthetic_name");

QString PageAttributesFashionStyleAesthetic::getId() const
{
    return QStringLiteral("PageAttributesFashionStyleAesthetic");
}

QString PageAttributesFashionStyleAesthetic::getName() const
{
    return QObject::tr("Fashion Style/Aesthetic");
}

QString PageAttributesFashionStyleAesthetic::getDescription() const
{
    return QObject::tr("Attributes defining a fashion style/aesthetic (e.g. Old Money, Y2K Retro, Boho Chic)");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionStyleAesthetic::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_NAME
                            , tr("Name")
                            , tr("The name of the style/aesthetic")
                            , tr("Old Money")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The style name can't be empty");
                                }
                                return QString{};
                            }
    };

    return attributes;
}
