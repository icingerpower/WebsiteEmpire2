#include <QObject>

#include "PageAttributesFashionCulture.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionCulture);

const QString PageAttributesFashionCulture::ID_NAME        = QStringLiteral("fashion_culture_name");
const QString PageAttributesFashionCulture::ID_DESCRIPTION = QStringLiteral("fashion_culture_description");

QString PageAttributesFashionCulture::getId() const
{
    return QStringLiteral("PageAttributesFashionCulture");
}

QString PageAttributesFashionCulture::getName() const
{
    return QObject::tr("Fashion Culture/Audience Segment");
}

QString PageAttributesFashionCulture::getDescription() const
{
    return QObject::tr("Attributes defining a cultural/audience segment (e.g. Western, Muslim/Modest, South Asian)");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionCulture::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_NAME
                            , tr("Name")
                            , tr("The name of the cultural/audience segment")
                            , tr("Muslim/Modest")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The culture name can't be empty");
                                }
                                return QString{};
                            }
    };

    *attributes << Attribute{ID_DESCRIPTION
                            , tr("Description")
                            , tr("Free-text description (optional)")
                            , tr("Modesty-driven fashion audience across religious and secular contexts")
                            , QString{}
                            , [](const QString &) { return QString{}; }
                            , std::nullopt
                            , true // optional
    };

    return attributes;
}
