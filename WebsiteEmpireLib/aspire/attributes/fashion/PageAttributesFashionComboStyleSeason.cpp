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
