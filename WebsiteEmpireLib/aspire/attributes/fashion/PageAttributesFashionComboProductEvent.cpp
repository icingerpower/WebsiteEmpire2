#include <QObject>

#include "PageAttributesFashionComboProductEvent.h"
#include "PageAttributesFashionProductType.h"
#include "PageAttributesFashionEvent.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboProductEvent);

const QString PageAttributesFashionComboProductEvent::ID_PRODUCT_TYPE = QStringLiteral("cpe_product_type");
const QString PageAttributesFashionComboProductEvent::ID_EVENT        = QStringLiteral("cpe_event");

const QString PageAttributesFashionComboProductEvent::FORMULA_WHAT_PRODUCT_TO_WEAR = QStringLiteral("what_product_to_wear_to_event");

QString PageAttributesFashionComboProductEvent::getId() const
{
    return QStringLiteral("PageAttributesFashionComboProductEvent");
}

QString PageAttributesFashionComboProductEvent::getName() const
{
    return QObject::tr("Combo: Product + Event");
}

QString PageAttributesFashionComboProductEvent::getDescription() const
{
    return QObject::tr("what {product} to wear to {event}");
}

QStringList PageAttributesFashionComboProductEvent::allowedFormulaIds() const
{
    return {FORMULA_WHAT_PRODUCT_TO_WEAR};
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboProductEvent::getAttributes() const
{
    auto attributes = PageAttributesFashionComboBase::getAttributes();

    *attributes << Attribute{ID_PRODUCT_TYPE
                            , tr("Product Type")
                            , tr("The product-type slot of this combination")
                            , tr("Heels")
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
