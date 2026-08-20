#include <QObject>

#include "PageAttributesFashionComboProductPattern.h"
#include "PageAttributesFashionProductType.h"
#include "PageAttributesFashionPattern.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboProductPattern);

const QString PageAttributesFashionComboProductPattern::ID_PRODUCT_TYPE = QStringLiteral("cpp_product_type");
const QString PageAttributesFashionComboProductPattern::ID_PATTERN      = QStringLiteral("cpp_pattern");

const QString PageAttributesFashionComboProductPattern::FORMULA_PRINT_PATTERN_STYLING = QStringLiteral("print_pattern_styling");

QString PageAttributesFashionComboProductPattern::getId() const
{
    return QStringLiteral("PageAttributesFashionComboProductPattern");
}

QString PageAttributesFashionComboProductPattern::getName() const
{
    return QObject::tr("Combo: Product + Pattern");
}

QString PageAttributesFashionComboProductPattern::getDescription() const
{
    return QObject::tr("outfit with {pattern} {product}");
}

QStringList PageAttributesFashionComboProductPattern::allowedFormulaIds() const
{
    return {FORMULA_PRINT_PATTERN_STYLING};
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboProductPattern::getAttributes() const
{
    auto attributes = PageAttributesFashionComboBase::getAttributes();

    *attributes << Attribute{ID_PRODUCT_TYPE
                            , tr("Product Type")
                            , tr("The product-type slot of this combination")
                            , tr("Midi Dress")
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

    *attributes << Attribute{ID_PATTERN
                            , tr("Pattern")
                            , tr("The print/pattern slot of this combination")
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

    return attributes;
}
