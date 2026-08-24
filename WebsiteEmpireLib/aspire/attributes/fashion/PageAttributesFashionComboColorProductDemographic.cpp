#include <QObject>

#include "PageAttributesFashionComboColorProductDemographic.h"
#include "PageAttributesFashionColor.h"
#include "PageAttributesFashionProductType.h"
#include "PageAttributesFashionDemographic.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboColorProductDemographic);

const QString PageAttributesFashionComboColorProductDemographic::ID_COLOR        = QStringLiteral("ccpd_color");
const QString PageAttributesFashionComboColorProductDemographic::ID_PRODUCT_TYPE = QStringLiteral("ccpd_product_type");
const QString PageAttributesFashionComboColorProductDemographic::ID_DEMOGRAPHIC  = QStringLiteral("ccpd_demographic");

const QString PageAttributesFashionComboColorProductDemographic::FORMULA_COLOR_PRODUCT_FOR_DEMOGRAPHIC = QStringLiteral("color_product_for_demographic");

QString PageAttributesFashionComboColorProductDemographic::getId() const
{
    return QStringLiteral("PageAttributesFashionComboColorProductDemographic");
}

QString PageAttributesFashionComboColorProductDemographic::getName() const
{
    return QObject::tr("Combo: Color + Product + Demographic");
}

QString PageAttributesFashionComboColorProductDemographic::getDescription() const
{
    return QObject::tr("{demographic} {color} {product}");
}

QStringList PageAttributesFashionComboColorProductDemographic::allowedFormulaIds() const
{
    return {FORMULA_COLOR_PRODUCT_FOR_DEMOGRAPHIC};
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboColorProductDemographic::getAttributes() const
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

    return attributes;
}
