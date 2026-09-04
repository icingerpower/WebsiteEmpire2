#include <QObject>

#include "PageAttributesFashionComboFitProductEvent.h"
#include "PageAttributesFashionFitSilhouette.h"
#include "PageAttributesFashionProductType.h"
#include "PageAttributesFashionEvent.h"

DECLARE_PAGE_ATTRIBUTES(PageAttributesFashionComboFitProductEvent);

const QString PageAttributesFashionComboFitProductEvent::ID_FIT          = QStringLiteral("cfpe_fit");
const QString PageAttributesFashionComboFitProductEvent::ID_PRODUCT_TYPE = QStringLiteral("cfpe_product_type");
const QString PageAttributesFashionComboFitProductEvent::ID_EVENT        = QStringLiteral("cfpe_event");

const QString PageAttributesFashionComboFitProductEvent::FORMULA_SILHOUETTE_OCCASION = QStringLiteral("silhouette_occasion");

QString PageAttributesFashionComboFitProductEvent::getId() const
{
    return QStringLiteral("PageAttributesFashionComboFitProductEvent");
}

QString PageAttributesFashionComboFitProductEvent::getName() const
{
    return QObject::tr("Combo: Fit + Product + Event");
}

QString PageAttributesFashionComboFitProductEvent::getDescription() const
{
    return QObject::tr("{fit/cut} {product} for {event}");
}

QStringList PageAttributesFashionComboFitProductEvent::allowedFormulaIds() const
{
    return {FORMULA_SILHOUETTE_OCCASION};
}

QString PageAttributesFashionComboFitProductEvent::composeArticleTopic(const QHash<QString, QString> &rowValues) const
{
    return rowValues.value(ID_FIT) + QLatin1Char(' ') + rowValues.value(ID_PRODUCT_TYPE)
         + QStringLiteral(" for ") + rowValues.value(ID_EVENT);
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesFashionComboFitProductEvent::getAttributes() const
{
    auto attributes = PageAttributesFashionComboBase::getAttributes();

    *attributes << Attribute{ID_FIT
                            , tr("Fit/Silhouette")
                            , tr("The fit/silhouette slot of this combination")
                            , tr("Maxi")
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

    *attributes << Attribute{ID_EVENT
                            , tr("Event")
                            , tr("The event/occasion slot of this combination")
                            , tr("Summer Wedding")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The event can't be empty");
                                }
                                return QString{};
                            }
                            , std::nullopt
                            , false // mandatory
                            , ReferenceSpec{PageAttributesFashionEvent::ID_NAME, ReferenceSpec::Cardinality::Single}
    };

    return attributes;
}
