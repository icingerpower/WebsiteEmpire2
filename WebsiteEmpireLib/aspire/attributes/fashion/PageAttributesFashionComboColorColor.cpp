#include <QObject>

#include "PageAttributesFashionComboColorColor.h"
#include "PageAttributesFashionColor.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboColorColor);

const QString PageAttributesFashionComboColorColor::ID_COLOR_A = QStringLiteral("ccc_color_a");
const QString PageAttributesFashionComboColorColor::ID_COLOR_B = QStringLiteral("ccc_color_b");

const QString PageAttributesFashionComboColorColor::FORMULA_COLOR_PAIRING      = QStringLiteral("color_pairing");
const QString PageAttributesFashionComboColorColor::FORMULA_DOES_COLOR_GO_WITH = QStringLiteral("does_color_go_with");

QString PageAttributesFashionComboColorColor::getId() const
{
    return QStringLiteral("PageAttributesFashionComboColorColor");
}

QString PageAttributesFashionComboColorColor::getName() const
{
    return QObject::tr("Combo: Color + Color");
}

QString PageAttributesFashionComboColorColor::getDescription() const
{
    return QObject::tr("{color} and {color} outfit combination / does {color} go with {color}");
}

QStringList PageAttributesFashionComboColorColor::allowedFormulaIds() const
{
    return {FORMULA_COLOR_PAIRING, FORMULA_DOES_COLOR_GO_WITH};
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboColorColor::getAttributes() const
{
    auto attributes = PageAttributesFashionComboBase::getAttributes();

    *attributes << Attribute{ID_COLOR_A
                            , tr("Color A")
                            , tr("The first color slot of this combination")
                            , tr("Red")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("Color A can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , false // mandatory
                            , ReferenceSpec{PageAttributesFashionColor::ID_NAME, ReferenceSpec::Cardinality::Single}
    };

    *attributes << Attribute{ID_COLOR_B
                            , tr("Color B")
                            , tr("The second color slot of this combination")
                            , tr("Pink")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("Color B can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , false // mandatory
                            , ReferenceSpec{PageAttributesFashionColor::ID_NAME, ReferenceSpec::Cardinality::Single}
    };

    return attributes;
}

QString PageAttributesFashionComboColorColor::areAttributesCrossValid(
    const QHash<QString, QString> &id_values) const
{
    const QString colorA = id_values.value(ID_COLOR_A);
    const QString colorB = id_values.value(ID_COLOR_B);

    if (!colorA.isEmpty() && colorA == colorB) {
        return tr("Color A and Color B must be different colors");
    }

    return QString{};
}

QString PageAttributesFashionComboColorColor::composeArticleTopic(const QHash<QString, QString> &rowValues) const
{
    const QString &colorA  = rowValues.value(ID_COLOR_A);
    const QString &colorB  = rowValues.value(ID_COLOR_B);
    const QString &formula = rowValues.value(PageAttributesFashionComboBase::ID_FORMULA_ID);

    if (formula == FORMULA_DOES_COLOR_GO_WITH) {
        return QStringLiteral("Does ") + colorA + QStringLiteral(" go with ") + colorB;
    }
    return colorA + QStringLiteral(" and ") + colorB + QStringLiteral(" outfit combination");
}
