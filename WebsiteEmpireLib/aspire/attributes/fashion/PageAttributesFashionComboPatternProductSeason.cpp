#include <QObject>

#include "PageAttributesFashionComboPatternProductSeason.h"
#include "PageAttributesFashionPattern.h"
#include "PageAttributesFashionProductType.h"
#include "PageAttributesFashionSeason.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboPatternProductSeason);

const QString PageAttributesFashionComboPatternProductSeason::ID_PATTERN      = QStringLiteral("cpps_pattern");
const QString PageAttributesFashionComboPatternProductSeason::ID_PRODUCT_TYPE = QStringLiteral("cpps_product_type");
const QString PageAttributesFashionComboPatternProductSeason::ID_SEASON       = QStringLiteral("cpps_season");

const QString PageAttributesFashionComboPatternProductSeason::FORMULA_PATTERN_PRODUCT_FOR_SEASON = QStringLiteral("pattern_product_for_season");

QString PageAttributesFashionComboPatternProductSeason::getId() const
{
    return QStringLiteral("PageAttributesFashionComboPatternProductSeason");
}

QString PageAttributesFashionComboPatternProductSeason::getName() const
{
    return QObject::tr("Combo: Pattern + Product + Season");
}

QString PageAttributesFashionComboPatternProductSeason::getDescription() const
{
    return QObject::tr("{pattern} {product} for {season}");
}

QStringList PageAttributesFashionComboPatternProductSeason::allowedFormulaIds() const
{
    return {FORMULA_PATTERN_PRODUCT_FOR_SEASON};
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboPatternProductSeason::getAttributes() const
{
    auto attributes = PageAttributesFashionComboBase::getAttributes();

    *attributes << Attribute{ID_PATTERN
                            , tr("Pattern")
                            , tr("The pattern/print slot of this combination")
                            , tr("Floral")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The pattern can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , false // mandatory
                            , ReferenceSpec{PageAttributesFashionPattern::ID_NAME, ReferenceSpec::Cardinality::Single}
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
