#include <QObject>

#include "PageAttributesFashionComboFitProductDemographic.h"
#include "PageAttributesFashionFitSilhouette.h"
#include "PageAttributesFashionProductType.h"
#include "PageAttributesFashionDemographic.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboFitProductDemographic);

const QString PageAttributesFashionComboFitProductDemographic::ID_FIT          = QStringLiteral("cfpd_fit");
const QString PageAttributesFashionComboFitProductDemographic::ID_PRODUCT_TYPE = QStringLiteral("cfpd_product_type");
const QString PageAttributesFashionComboFitProductDemographic::ID_DEMOGRAPHIC  = QStringLiteral("cfpd_demographic");

const QString PageAttributesFashionComboFitProductDemographic::FORMULA_FIT_RECOMMENDATION = QStringLiteral("fit_recommendation");

QString PageAttributesFashionComboFitProductDemographic::getId() const
{
    return QStringLiteral("PageAttributesFashionComboFitProductDemographic");
}

QString PageAttributesFashionComboFitProductDemographic::getName() const
{
    return QObject::tr("Combo: Fit + Product + Demographic");
}

QString PageAttributesFashionComboFitProductDemographic::getDescription() const
{
    return QObject::tr("Best {fit} {product} for {body shape/target}");
}

QStringList PageAttributesFashionComboFitProductDemographic::allowedFormulaIds() const
{
    return {FORMULA_FIT_RECOMMENDATION};
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboFitProductDemographic::getAttributes() const
{
    auto attributes = PageAttributesFashionComboBase::getAttributes();

    *attributes << Attribute{ID_FIT
                            , tr("Fit/Silhouette")
                            , tr("The fit/silhouette slot of this combination")
                            , tr("Wide-Leg")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The fit can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , false // mandatory
                            , ReferenceSpec{PageAttributesFashionFitSilhouette::ID_NAME, ReferenceSpec::Cardinality::Single}
    };

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
                            , tr("The target demographic/body-type slot of this combination")
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
