#include <QObject>

#include "PageAttributesFashionComboMaterialProduct.h"
#include "PageAttributesFashionMaterial.h"
#include "PageAttributesFashionProductType.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboMaterialProduct);

const QString PageAttributesFashionComboMaterialProduct::ID_MATERIAL     = QStringLiteral("cmp_material");
const QString PageAttributesFashionComboMaterialProduct::ID_PRODUCT_TYPE = QStringLiteral("cmp_product_type");

const QString PageAttributesFashionComboMaterialProduct::FORMULA_HOW_TO_STYLE_MATERIAL_PRODUCT = QStringLiteral("how_to_style_material_product");

QString PageAttributesFashionComboMaterialProduct::getId() const
{
    return QStringLiteral("PageAttributesFashionComboMaterialProduct");
}

QString PageAttributesFashionComboMaterialProduct::getName() const
{
    return QObject::tr("Combo: Material + Product");
}

QString PageAttributesFashionComboMaterialProduct::getDescription() const
{
    return QObject::tr("how to style a {material} {product}");
}

QStringList PageAttributesFashionComboMaterialProduct::allowedFormulaIds() const
{
    return {FORMULA_HOW_TO_STYLE_MATERIAL_PRODUCT};
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboMaterialProduct::getAttributes() const
{
    auto attributes = PageAttributesFashionComboBase::getAttributes();

    *attributes << Attribute{ID_MATERIAL
                            , tr("Material")
                            , tr("The material/fabric slot of this combination")
                            , tr("Silk")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The material can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , false // mandatory
                            , ReferenceSpec{PageAttributesFashionMaterial::ID_NAME, ReferenceSpec::Cardinality::Single}
    };

    *attributes << Attribute{ID_PRODUCT_TYPE
                            , tr("Product Type")
                            , tr("The product-type slot of this combination")
                            , tr("Skirt")
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
