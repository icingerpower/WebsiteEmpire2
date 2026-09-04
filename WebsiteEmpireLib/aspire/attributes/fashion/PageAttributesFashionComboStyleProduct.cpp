#include <QObject>

#include "PageAttributesFashionComboStyleProduct.h"
#include "PageAttributesFashionStyleAesthetic.h"
#include "PageAttributesFashionProductType.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboStyleProduct);

const QString PageAttributesFashionComboStyleProduct::ID_STYLE        = QStringLiteral("csp_style");
const QString PageAttributesFashionComboStyleProduct::ID_PRODUCT_TYPE = QStringLiteral("csp_product_type");

const QString PageAttributesFashionComboStyleProduct::FORMULA_STYLE_PRODUCT_OUTFITS = QStringLiteral("style_product_outfits");

QString PageAttributesFashionComboStyleProduct::getId() const
{
    return QStringLiteral("PageAttributesFashionComboStyleProduct");
}

QString PageAttributesFashionComboStyleProduct::getName() const
{
    return QObject::tr("Combo: Style + Product");
}

QString PageAttributesFashionComboStyleProduct::getDescription() const
{
    return QObject::tr("{style} {product} outfits");
}

QStringList PageAttributesFashionComboStyleProduct::allowedFormulaIds() const
{
    return {FORMULA_STYLE_PRODUCT_OUTFITS};
}

QString PageAttributesFashionComboStyleProduct::composeArticleTopic(const QHash<QString, QString> &rowValues) const
{
    return rowValues.value(ID_STYLE) + QLatin1Char(' ') + rowValues.value(ID_PRODUCT_TYPE)
         + QStringLiteral(" outfits");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboStyleProduct::getAttributes() const
{
    auto attributes = PageAttributesFashionComboBase::getAttributes();

    *attributes << Attribute{ID_STYLE
                            , tr("Style/Aesthetic")
                            , tr("The style/aesthetic slot of this combination")
                            , tr("Old Money")
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
                            , tr("Blazer")
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
