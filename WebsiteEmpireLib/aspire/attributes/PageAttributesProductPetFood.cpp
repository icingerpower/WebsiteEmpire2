#include <QObject>

#include "PageAttributesProductPetFood.h"

const QString PageAttributesProductPetFood::ID_WEIGHT_GR = "weight_gr";
const QString PageAttributesProductPetFood::ID_PRICES = "prices";

DECLARE_PAGE_ATTRIBUTES(PageAttributesProductPetFood);

QString PageAttributesProductPetFood::getId() const
{
    return "PageAttributesProductPetFood";
}

QString PageAttributesProductPetFood::getName() const
{
    return QObject::tr("Pet Food Product");
}

QString PageAttributesProductPetFood::getDescription() const
{
    return QObject::tr("Attributes defining a pet food product");
}

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesProductPetFood::getAttributes() const
{
    auto attributes = PageAttributesProduct::getAttributes();

    *attributes << Attribute{ID_WEIGHT_GR
                            , tr("Weight (g)")
                            , tr("Weight in grams. Multiple values separated by ;")
                            , tr("500;1000;2000")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The weight can't be empty");
                                }
                                const QStringList parts = value.split(';');
                                for (const QString &part : parts) {
                                    bool ok;
                                    part.trimmed().toInt(&ok);
                                    if (!ok) {
                                        return tr("Each weight must be a valid integer (got: \"%1\")").arg(part.trimmed());
                                    }
                                }
                                return QString{};
                            }
    };

    *attributes << Attribute{ID_PRICES
                            , tr("Prices")
                            , tr("Prices. Multiple values separated by ;")
                            , tr("12.99;24.99;44.99")
                            , QString{}
                            , [](const QString &value) {
                                if (value.isEmpty()) {
                                    return tr("The prices can't be empty");
                                }
                                const QStringList parts = value.split(';');
                                for (const QString &part : parts) {
                                    bool ok;
                                    part.trimmed().toDouble(&ok);
                                    if (!ok) {
                                        return tr("Each price must be a valid number (got: \"%1\")").arg(part.trimmed());
                                    }
                                }
                                return QString{};
                            }
    };

    return attributes;
}

QString PageAttributesProductPetFood::areAttributesCrossValid(const QHash<QString, QString> &id_values) const
{
    const QString parentError = PageAttributesProduct::areAttributesCrossValid(id_values);
    if (!parentError.isEmpty()) {
        return parentError;
    }

    const QString weightValue = id_values.value(ID_WEIGHT_GR);
    const QString pricesValue = id_values.value(ID_PRICES);

    if (weightValue.isEmpty() || pricesValue.isEmpty()) {
        return QString{};
    }

    const int weightCount = weightValue.split(';').size();
    const int pricesCount = pricesValue.split(';').size();

    if (weightCount != pricesCount) {
        return tr("Weight (%1 value(s)) and Prices (%2 value(s)) must have the same number of entries")
            .arg(weightCount)
            .arg(pricesCount);
    }

    return QString{};
}
