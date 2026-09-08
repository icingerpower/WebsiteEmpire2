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

bool PageAttributesFashionComboColorProduct::isArticleTopicEligible(const QHash<QString, QString> &rowValues) const
{
    // FORMULA_FOOTWEAR_MATCHING renders "What shoes to wear with {color}
    // {product}". Excluded from article generation for two independent reasons:
    //
    //  1. Incoherent whenever the product IS footwear — "what shoes to wear
    //     with black boots", "what shoes to wear with blue heels". 1,589 of its
    //     ~10,400 rows pair it with one of the 19 footwear product types. The
    //     taxonomy generator never caught this: its AI filter only judged
    //     CULTURAL appropriateness of a color+product pairing, never whether
    //     the resulting query phrasing made sense for that product.
    //  2. Even where it is coherent ("what shoes to wear with a black midi
    //     dress"), keyword research put these SERPs at ~50% shopping/PLP
    //     results, so a content-only site cannot realistically rank for them.
    //
    // To re-enable only the coherent subset later, narrow this to reject the
    // formula solely when the product type is footwear.
    //
    // FORMULA_HOW_TO_STYLE is excluded for a different reason: it and
    // FORMULA_STYLING_PAIRING are the SAME search intent. Keyword research put
    // the top-ranking URL overlap between "how to style {x}" and "what to wear
    // with {x}" at 80-90%, so publishing both per color+product pair means two
    // of our own pages competing for one query cluster — self-cannibalization,
    // and at ~10k pairs it also risks Google's scaled-content/helpful-content
    // filters. One page per pair instead — FORMULA_STYLING_PAIRING is the
    // survivor, so the URL stays /what-to-wear-with-{x}-outfit-ideas and the
    // article's own H1 carries the "how to style" phrasing.
    //
    // composeArticleTopic() deliberately still handles both vetoed formulas:
    // the veto is an editorial choice that may be narrowed later, and a row
    // that becomes eligible again must not fall through to a wrong phrasing.
    const QString &formula = rowValues.value(PageAttributesFashionComboBase::ID_FORMULA_ID);
    return formula != FORMULA_FOOTWEAR_MATCHING && formula != FORMULA_HOW_TO_STYLE;
}

QString PageAttributesFashionComboColorProduct::composeArticleTopic(const QHash<QString, QString> &rowValues) const
{
    const QString &color   = rowValues.value(ID_COLOR);
    const QString &product = rowValues.value(ID_PRODUCT_TYPE);
    const QString &formula = rowValues.value(PageAttributesFashionComboBase::ID_FORMULA_ID);

    if (formula == FORMULA_FOOTWEAR_MATCHING) {
        return QStringLiteral("What shoes to wear with ") + color + QLatin1Char(' ') + product;
    }
    if (formula == FORMULA_HOW_TO_STYLE) {
        return QStringLiteral("How to style ") + color + QLatin1Char(' ') + product;
    }
    // The one surviving formula (see isArticleTopicEligible()): kept short so
    // the permalink stays clean. The AI's own H1 covers the "how to style X"
    // phrasing too, which is the same search intent, so one URL serves both.
    return QStringLiteral("What to wear with ") + color + QLatin1Char(' ') + product;
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
