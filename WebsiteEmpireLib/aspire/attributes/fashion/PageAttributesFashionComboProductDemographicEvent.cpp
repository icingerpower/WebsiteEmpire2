#include <QObject>

#include "PageAttributesFashionComboProductDemographicEvent.h"
#include "PageAttributesFashionProductType.h"
#include "PageAttributesFashionDemographic.h"
#include "PageAttributesFashionEvent.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboProductDemographicEvent);

const QString PageAttributesFashionComboProductDemographicEvent::ID_PRODUCT_TYPE = QStringLiteral("cpde_product_type");
const QString PageAttributesFashionComboProductDemographicEvent::ID_DEMOGRAPHIC  = QStringLiteral("cpde_demographic");
const QString PageAttributesFashionComboProductDemographicEvent::ID_EVENT        = QStringLiteral("cpde_event");

const QString PageAttributesFashionComboProductDemographicEvent::FORMULA_PRODUCT_DEMOGRAPHIC_FOR_EVENT = QStringLiteral("product_demographic_for_event");

QString PageAttributesFashionComboProductDemographicEvent::getId() const
{
    return QStringLiteral("PageAttributesFashionComboProductDemographicEvent");
}

QString PageAttributesFashionComboProductDemographicEvent::getName() const
{
    return QObject::tr("Combo: Product + Demographic + Event");
}

QString PageAttributesFashionComboProductDemographicEvent::getDescription() const
{
    return QObject::tr("{demographic} {product} for {event}");
}

QStringList PageAttributesFashionComboProductDemographicEvent::allowedFormulaIds() const
{
    return {FORMULA_PRODUCT_DEMOGRAPHIC_FOR_EVENT};
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboProductDemographicEvent::getAttributes() const
{
    auto attributes = PageAttributesFashionComboBase::getAttributes();

    *attributes << Attribute{ID_PRODUCT_TYPE
                            , tr("Product Type")
                            , tr("The product-type slot of this combination")
                            , tr("Dress")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The product type can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , false // mandatory
                            , ReferenceSpec{PageAttributesFashionProductType::ID_NAME, ReferenceSpec::Cardinality::Single}
    };

    *attributes << Attribute{ID_DEMOGRAPHIC
                            , tr("Demographic")
                            , tr("The demographic/body-type slot of this combination")
                            , tr("Plus Size")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The demographic can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , false // mandatory
                            , ReferenceSpec{PageAttributesFashionDemographic::ID_NAME, ReferenceSpec::Cardinality::Single}
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
