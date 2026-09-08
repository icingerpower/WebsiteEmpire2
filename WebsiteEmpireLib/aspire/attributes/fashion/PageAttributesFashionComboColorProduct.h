#ifndef PAGEATTRIBUTESFASHIONCOMBOCOLORPRODUCT_H
#define PAGEATTRIBUTESFASHIONCOMBOCOLORPRODUCT_H

#include "PageAttributesFashionComboBase.h"

// Combination of Color + ProductType. Covers study formulas #1 ("what to
// wear with {color} {product}"), #5 ("what shoes to wear with {color}
// {product}") and #6 ("how to style {color} {product}") — these three share
// an identical slot signature and differ only by query template/intent,
// distinguished via ID_FORMULA_ID.
class PageAttributesFashionComboColorProduct : public PageAttributesFashionComboBase
{
    Q_OBJECT

public:
    static const QString ID_COLOR;
    static const QString ID_PRODUCT_TYPE;

    static const QString FORMULA_STYLING_PAIRING;
    static const QString FORMULA_FOOTWEAR_MATCHING;
    static const QString FORMULA_HOW_TO_STYLE;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QStringList allowedFormulaIds() const override;
    QString composeArticleTopic(const QHash<QString, QString> &rowValues) const override;

    /**
     * Leaves only FORMULA_STYLING_PAIRING eligible for article generation, so
     * each color+product pair yields exactly one article:
     *   - FORMULA_FOOTWEAR_MATCHING is vetoed for two independent reasons
     *     (nonsense when the product IS footwear; shopping-dominated SERPs).
     *   - FORMULA_HOW_TO_STYLE is vetoed as the same search intent as
     *     FORMULA_STYLING_PAIRING (80-90% top-ranking URL overlap), which would
     *     make our own two pages compete for one query cluster.
     * See the .cpp for the full reasoning. The rows remain in the aspire
     * research DB — only article generation skips them.
     */
    bool isArticleTopicEligible(const QHash<QString, QString> &rowValues) const override;
};

#endif // PAGEATTRIBUTESFASHIONCOMBOCOLORPRODUCT_H
