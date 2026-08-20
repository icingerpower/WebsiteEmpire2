#include <QObject>

#include "PageAttributesFashionComboColorProduct.h"
#include "PageAttributesFashionColor.h"
#include "PageAttributesFashionProductType.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboColorProduct);

const QString PageAttributesFashionComboColorProduct::ID_COLOR        = QStringLiteral("ccp_color");
const QString PageAttributesFashionComboColorProduct::ID_PRODUCT_TYPE = QStringLiteral("ccp_product_type");

const QString PageAttributesFashionComboColorProduct::FORMULA_STYLING_PAIRING   = QStringLiteral("styling_pairing");
const QString PageAttributesFashionComboColorProduct::FORMULA_FOOTWEAR_MATCHING = QStringLiteral("footwear_matching");
const QString PageAttributesFashionComboColorProduct::FORMULA_HOW_TO_STYLE      = QStringLiteral("how_to_style");

QString PageAttributesFashionComboColorProduct::getId() const
{
    return QStringLiteral("PageAttributesFashionComboColorProduct");
}

QString PageAttributesFashionComboColorProduct::getName() const
{
    return QObject::tr("Combo: Color + Product");
}

QString PageAttributesFashionComboColorProduct::getDescription() const
{
    return QObject::tr("What to wear with / what shoes to wear with / how to style {color} {product}");
}

QStringList PageAttributesFashionComboColorProduct::allowedFormulaIds() const
{
    return {FORMULA_STYLING_PAIRING, FORMULA_FOOTWEAR_MATCHING, FORMULA_HOW_TO_STYLE};
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboColorProduct::getAttributes() const
{
    auto attributes = PageAttributesFashionComboBase::getAttributes();

    *attributes << Attribute{ID_COLOR
                            , tr("Color")
                            , tr("The color slot of this combination")
                            , tr("Red")
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

    return attributes;
}
