#include <QObject>

#include "PageAttributesFashionComboStyleProductEvent.h"
#include "PageAttributesFashionStyleAesthetic.h"
#include "PageAttributesFashionProductType.h"
#include "PageAttributesFashionEvent.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboStyleProductEvent);

const QString PageAttributesFashionComboStyleProductEvent::ID_STYLE        = QStringLiteral("cspe_style");
const QString PageAttributesFashionComboStyleProductEvent::ID_PRODUCT_TYPE = QStringLiteral("cspe_product_type");
const QString PageAttributesFashionComboStyleProductEvent::ID_EVENT        = QStringLiteral("cspe_event");

const QString PageAttributesFashionComboStyleProductEvent::FORMULA_STYLE_PRODUCT_FOR_EVENT = QStringLiteral("style_product_for_event");

QString PageAttributesFashionComboStyleProductEvent::getId() const
{
    return QStringLiteral("PageAttributesFashionComboStyleProductEvent");
}

QString PageAttributesFashionComboStyleProductEvent::getName() const
{
    return QObject::tr("Combo: Style + Product + Event");
}

QString PageAttributesFashionComboStyleProductEvent::getDescription() const
{
    return QObject::tr("{style} {product} for {event}");
}

QStringList PageAttributesFashionComboStyleProductEvent::allowedFormulaIds() const
{
    return {FORMULA_STYLE_PRODUCT_FOR_EVENT};
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboStyleProductEvent::getAttributes() const
{
    auto attributes = PageAttributesFashionComboBase::getAttributes();

    *attributes << Attribute{ID_STYLE
                            , tr("Style/Aesthetic")
                            , tr("The style/aesthetic slot of this combination")
                            , tr("Boho Chic")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The style can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , false // mandatory
                            , ReferenceSpec{PageAttributesFashionStyleAesthetic::ID_NAME, ReferenceSpec::Cardinality::Single}
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
                            , tr("Music Festival")
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
