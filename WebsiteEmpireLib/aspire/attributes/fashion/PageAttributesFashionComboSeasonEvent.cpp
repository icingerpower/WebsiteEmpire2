#include <QObject>

#include "PageAttributesFashionComboSeasonEvent.h"
#include "PageAttributesFashionSeason.h"
#include "PageAttributesFashionEvent.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboSeasonEvent);

const QString PageAttributesFashionComboSeasonEvent::ID_SEASON = QStringLiteral("cse_season");
const QString PageAttributesFashionComboSeasonEvent::ID_EVENT  = QStringLiteral("cse_event");

const QString PageAttributesFashionComboSeasonEvent::FORMULA_OCCASION_SEASONALITY = QStringLiteral("occasion_seasonality");

QString PageAttributesFashionComboSeasonEvent::getId() const
{
    return QStringLiteral("PageAttributesFashionComboSeasonEvent");
}

QString PageAttributesFashionComboSeasonEvent::getName() const
{
    return QObject::tr("Combo: Season + Event");
}

QString PageAttributesFashionComboSeasonEvent::getDescription() const
{
    return QObject::tr("{season} {event} outfit ideas");
}

QStringList PageAttributesFashionComboSeasonEvent::allowedFormulaIds() const
{
    return {FORMULA_OCCASION_SEASONALITY};
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboSeasonEvent::getAttributes() const
{
    auto attributes = PageAttributesFashionComboBase::getAttributes();

    *attributes << Attribute{ID_SEASON
                            , tr("Season")
                            , tr("The season slot of this combination")
                            , tr("Summer")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The season can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , false // mandatory
                            , ReferenceSpec{PageAttributesFashionSeason::ID_NAME, ReferenceSpec::Cardinality::Single}
    };

    *attributes << Attribute{ID_EVENT
                            , tr("Event")
                            , tr("The event/occasion slot of this combination")
                            , tr("Wedding Guest")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The event can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , false // mandatory
                            , ReferenceSpec{PageAttributesFashionEvent::ID_NAME, ReferenceSpec::Cardinality::Single}
    };

    return attributes;
}
