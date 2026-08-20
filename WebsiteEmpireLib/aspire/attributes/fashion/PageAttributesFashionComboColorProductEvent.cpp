#include <QObject>

#include "PageAttributesFashionComboColorProductEvent.h"
#include "PageAttributesFashionColor.h"
#include "PageAttributesFashionProductType.h"
#include "PageAttributesFashionEvent.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboColorProductEvent);

const QString PageAttributesFashionComboColorProductEvent::ID_COLOR        = QStringLiteral("ccpe_color");
const QString PageAttributesFashionComboColorProductEvent::ID_PRODUCT_TYPE = QStringLiteral("ccpe_product_type");
const QString PageAttributesFashionComboColorProductEvent::ID_EVENT        = QStringLiteral("ccpe_event");

const QString PageAttributesFashionComboColorProductEvent::FORMULA_DIRECT_TRANSACTIONAL = QStringLiteral("direct_transactional");

QString PageAttributesFashionComboColorProductEvent::getId() const
{
    return QStringLiteral("PageAttributesFashionComboColorProductEvent");
}

QString PageAttributesFashionComboColorProductEvent::getName() const
{
    return QObject::tr("Combo: Color + Product + Event");
}

QString PageAttributesFashionComboColorProductEvent::getDescription() const
{
    return QObject::tr("{color} {product} for {event}");
}

QStringList PageAttributesFashionComboColorProductEvent::allowedFormulaIds() const
{
    return {FORMULA_DIRECT_TRANSACTIONAL};
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboColorProductEvent::getAttributes() const
{
    auto attributes = PageAttributesFashionComboBase::getAttributes();

    *attributes << Attribute{ID_COLOR
                            , tr("Color")
                            , tr("The color slot of this combination")
                            , tr("Black")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The color can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , false // mandatory
                            , ReferenceSpec{PageAttributesFashionColor::ID_NAME, ReferenceSpec::Cardinality::Single}
    };

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

    *attributes << Attribute{ID_EVENT
                            , tr("Event")
                            , tr("The event/occasion slot of this combination")
                            , tr("Funeral")
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
