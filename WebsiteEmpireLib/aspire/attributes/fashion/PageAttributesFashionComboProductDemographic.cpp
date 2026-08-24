#include <QObject>

#include "PageAttributesFashionComboProductDemographic.h"
#include "PageAttributesFashionProductType.h"
#include "PageAttributesFashionDemographic.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboProductDemographic);

const QString PageAttributesFashionComboProductDemographic::ID_PRODUCT_TYPE = QStringLiteral("cpd_product_type");
const QString PageAttributesFashionComboProductDemographic::ID_DEMOGRAPHIC  = QStringLiteral("cpd_demographic");

const QString PageAttributesFashionComboProductDemographic::FORMULA_BEST_PRODUCT_FOR_DEMOGRAPHIC = QStringLiteral("best_product_for_demographic");

QString PageAttributesFashionComboProductDemographic::getId() const
{
    return QStringLiteral("PageAttributesFashionComboProductDemographic");
}

QString PageAttributesFashionComboProductDemographic::getName() const
{
    return QObject::tr("Combo: Product + Demographic");
}

QString PageAttributesFashionComboProductDemographic::getDescription() const
{
    return QObject::tr("best {product} for {demographic}");
}

QStringList PageAttributesFashionComboProductDemographic::allowedFormulaIds() const
{
    return {FORMULA_BEST_PRODUCT_FOR_DEMOGRAPHIC};
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboProductDemographic::getAttributes() const
{
    auto attributes = PageAttributesFashionComboBase::getAttributes();

    *attributes << Attribute{ID_PRODUCT_TYPE
                            , tr("Product Type")
                            , tr("The product-type slot of this combination")
                            , tr("Jeans")
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
                            , tr("Petite")
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
