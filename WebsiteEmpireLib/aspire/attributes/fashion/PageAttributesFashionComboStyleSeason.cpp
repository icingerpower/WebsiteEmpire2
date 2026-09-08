#include <QObject>

#include "PageAttributesFashionComboStyleSeason.h"
#include "PageAttributesFashionStyleAesthetic.h"
#include "PageAttributesFashionSeason.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboStyleSeason);

const QString PageAttributesFashionComboStyleSeason::ID_STYLE  = QStringLiteral("css_style");
const QString PageAttributesFashionComboStyleSeason::ID_SEASON = QStringLiteral("css_season");

const QString PageAttributesFashionComboStyleSeason::FORMULA_MICROTREND_LIFESTYLE = QStringLiteral("microtrend_lifestyle");
const QString PageAttributesFashionComboStyleSeason::FORMULA_CAPSULE_CURATION     = QStringLiteral("capsule_curation");

QString PageAttributesFashionComboStyleSeason::getId() const
{
    return QStringLiteral("PageAttributesFashionComboStyleSeason");
}

QString PageAttributesFashionComboStyleSeason::getName() const
{
    return QObject::tr("Combo: Style + Season");
}

QString PageAttributesFashionComboStyleSeason::getDescription() const
{
    return QObject::tr("{aesthetic} {season} outfit ideas / capsule wardrobe {season} {style}");
}

QStringList PageAttributesFashionComboStyleSeason::allowedFormulaIds() const
{
    return {FORMULA_MICROTREND_LIFESTYLE, FORMULA_CAPSULE_CURATION};
}

QString PageAttributesFashionComboStyleSeason::composeArticleTopic(const QHash<QString, QString> &rowValues) const
{
    const QString &style   = rowValues.value(ID_STYLE);
    const QString &season  = rowValues.value(ID_SEASON);
    const QString &formula = rowValues.value(PageAttributesFashionComboBase::ID_FORMULA_ID);

    // Slot order follows how people actually type the query ("y2k summer
    // capsule wardrobe"), not the table's column order — the composed topic is
    // slugified into the permalink, so a reversed order costs exact-match
    // relevance for no benefit.
    //
    // Unlike PageAttributesFashionComboColorProduct, BOTH formulas here are
    // kept: keyword research put the top-10 URL overlap between the two at only
    // 10-20% (inspiration/lookbook SERPs vs. structured checklist SERPs), so
    // they are genuinely different intents and do not cannibalize each other.
    if (formula == FORMULA_CAPSULE_CURATION) {
        return style + QLatin1Char(' ') + season + QStringLiteral(" capsule wardrobe");
    }
    return style + QLatin1Char(' ') + season + QStringLiteral(" outfit ideas");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboStyleSeason::getAttributes() const
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
