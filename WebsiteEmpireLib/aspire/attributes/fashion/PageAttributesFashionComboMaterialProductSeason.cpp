#include <QObject>

#include "PageAttributesFashionComboMaterialProductSeason.h"
#include "PageAttributesFashionMaterial.h"
#include "PageAttributesFashionProductType.h"
#include "PageAttributesFashionSeason.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboMaterialProductSeason);

const QString PageAttributesFashionComboMaterialProductSeason::ID_MATERIAL     = QStringLiteral("cmps_material");
const QString PageAttributesFashionComboMaterialProductSeason::ID_PRODUCT_TYPE = QStringLiteral("cmps_product_type");
const QString PageAttributesFashionComboMaterialProductSeason::ID_SEASON       = QStringLiteral("cmps_season");

const QString PageAttributesFashionComboMaterialProductSeason::FORMULA_FABRIC_WEATHER = QStringLiteral("fabric_weather");

QString PageAttributesFashionComboMaterialProductSeason::getId() const
{
    return QStringLiteral("PageAttributesFashionComboMaterialProductSeason");
}

QString PageAttributesFashionComboMaterialProductSeason::getName() const
{
    return QObject::tr("Combo: Material + Product + Season");
}

QString PageAttributesFashionComboMaterialProductSeason::getDescription() const
{
    return QObject::tr("{material} {product} outfit {season}");
}

QStringList PageAttributesFashionComboMaterialProductSeason::allowedFormulaIds() const
{
    return {FORMULA_FABRIC_WEATHER};
}

QString PageAttributesFashionComboMaterialProductSeason::composeArticleTopic(const QHash<QString, QString> &rowValues) const
{
    return rowValues.value(ID_MATERIAL) + QLatin1Char(' ') + rowValues.value(ID_PRODUCT_TYPE)
         + QStringLiteral(" outfit ") + rowValues.value(ID_SEASON);
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboMaterialProductSeason::getAttributes() const
{
    auto attributes = PageAttributesFashionComboBase::getAttributes();

    *attributes << Attribute{ID_MATERIAL
                            , tr("Material")
                            , tr("The material/fabric slot of this combination")
                            , tr("Linen")
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
                            , tr("Pants")
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

    return attributes;
}
