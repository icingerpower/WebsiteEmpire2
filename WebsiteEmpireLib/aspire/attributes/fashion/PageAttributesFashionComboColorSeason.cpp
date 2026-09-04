#include <QObject>

#include "PageAttributesFashionComboColorSeason.h"
#include "PageAttributesFashionColor.h"
#include "PageAttributesFashionSeason.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboColorSeason);

const QString PageAttributesFashionComboColorSeason::ID_COLOR  = QStringLiteral("ccs_color");
const QString PageAttributesFashionComboColorSeason::ID_SEASON = QStringLiteral("ccs_season");

const QString PageAttributesFashionComboColorSeason::FORMULA_COLOR_SEASON_FASHION = QStringLiteral("color_season_fashion");

QString PageAttributesFashionComboColorSeason::getId() const
{
    return QStringLiteral("PageAttributesFashionComboColorSeason");
}

QString PageAttributesFashionComboColorSeason::getName() const
{
    return QObject::tr("Combo: Color + Season");
}

QString PageAttributesFashionComboColorSeason::getDescription() const
{
    return QObject::tr("best {color} outfits for {season}");
}

QStringList PageAttributesFashionComboColorSeason::allowedFormulaIds() const
{
    return {FORMULA_COLOR_SEASON_FASHION};
}

QString PageAttributesFashionComboColorSeason::composeArticleTopic(const QHash<QString, QString> &rowValues) const
{
    return QStringLiteral("Best ") + rowValues.value(ID_COLOR) + QStringLiteral(" outfits for ")
         + rowValues.value(ID_SEASON);
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboColorSeason::getAttributes() const
{
    auto attributes = PageAttributesFashionComboBase::getAttributes();

    *attributes << Attribute{ID_COLOR
                            , tr("Color")
                            , tr("The color slot of this combination")
                            , tr("Sage Green")
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

    *attributes << Attribute{ID_SEASON
                            , tr("Season")
                            , tr("The season slot of this combination")
                            , tr("Autumn/Fall")
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
