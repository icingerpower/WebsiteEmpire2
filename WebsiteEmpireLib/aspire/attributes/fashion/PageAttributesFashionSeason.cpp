#include <QObject>

#include "PageAttributesFashionSeason.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionSeason);

const QString PageAttributesFashionSeason::ID_NAME = QStringLiteral("fashion_season_name");

QString PageAttributesFashionSeason::getId() const
{
    return QStringLiteral("PageAttributesFashionSeason");
}

QString PageAttributesFashionSeason::getName() const
{
    return QObject::tr("Fashion Season");
}

QString PageAttributesFashionSeason::getDescription() const
{
    return QObject::tr("Attributes defining a fashion season or micro-season (e.g. Summer, Resort/Cruise)");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionSeason::getAttributes() const
{
    auto attributes = QSharedPointer<QList<AbstractPageAttributes::Attribute>>::create();

    *attributes << Attribute{ID_NAME
                            , tr("Name")
                            , tr("The name of the season or micro-season")
                            , tr("Summer")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The season name can't be empty");
                                }
                                return QString{};
                            }
    };

    return attributes;
}
