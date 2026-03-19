#include <QObject>

#include "PageAttributesProductFashion.h"

// ---------------------------------------------------------------------------
// ID constants
// ---------------------------------------------------------------------------

const QString PageAttributesProductFashion::ID_SIZES    = QStringLiteral("sizes");
const QString PageAttributesProductFashion::ID_PRICES   = QStringLiteral("prices");
const QString PageAttributesProductFashion::ID_SIZES_FR = QStringLiteral("sizes_fr");
const QString PageAttributesProductFashion::ID_COLORS   = QStringLiteral("colors");
const QString PageAttributesProductFashion::ID_SEXE     = QStringLiteral("sexe");
const QString PageAttributesProductFashion::ID_AGE      = QStringLiteral("age");
const QString PageAttributesProductFashion::ID_TYPE     = QStringLiteral("type");
const QString PageAttributesProductFashion::ID_MATERIAL = QStringLiteral("material");

// ---------------------------------------------------------------------------
// Constrained attribute values
// ---------------------------------------------------------------------------

const QString PageAttributesProductFashion::SEXE_MEN    = QStringLiteral("Men");
const QString PageAttributesProductFashion::SEXE_WOMEN  = QStringLiteral("Women");
const QString PageAttributesProductFashion::SEXE_UNISEX = QStringLiteral("Unisex");

const QString PageAttributesProductFashion::AGE_ADULT   = QStringLiteral("Adult");
const QString PageAttributesProductFashion::AGE_KID     = QStringLiteral("Kid");
const QString PageAttributesProductFashion::AGE_BABY    = QStringLiteral("Baby");

const QString PageAttributesProductFashion::TYPE_CLOTHE    = QStringLiteral("Clothe");
const QString PageAttributesProductFashion::TYPE_SHOES     = QStringLiteral("Shoes");
const QString PageAttributesProductFashion::TYPE_ACCESSORY = QStringLiteral("Accessory");

DECLARE_PAGE_ATTRIBUTES(PageAttributesProductFashion);

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

QString PageAttributesProductFashion::getId() const
{
    return QStringLiteral("PageAttributesProductFashion");
}

QString PageAttributesProductFashion::getName() const
{
    return QObject::tr("Fashion Product");
}

QString PageAttributesProductFashion::getDescription() const
{
    return QObject::tr("Attributes defining a fashion product (clothing, shoes, accessories)");
}

// ---------------------------------------------------------------------------
// Attributes
// ---------------------------------------------------------------------------

QSharedPointer<QList<AbstractPageAttributes::Attribute>> PageAttributesProductFashion::getAttributes() const
{
    auto attributes = PageAttributesProduct::getAttributes();

    // ID_SIZES — raw size labels, semicolon-separated, non-optional.
    *attributes << Attribute{
        ID_SIZES,
        tr("Sizes"),
        tr("Size labels available for this product, separated by ;"),
        tr("US-2 | UK/AU-6 | EU/DE-32 | FR/ES-34;US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36"),
        QString{},
        [](const QString &value) -> QString {
            if (value.isEmpty()) {
                return tr("Sizes cannot be empty");
            }
            return {};
        }
    };

    // ID_PRICES — one price per size variant, semicolon-separated, non-optional.
    // Each segment must be a valid non-negative number.
    *attributes << Attribute{
        ID_PRICES,
        tr("Prices"),
        tr("Selling price for each size variant, separated by ;"),
        tr("29.99;29.99;34.99"),
        QString{},
        [](const QString &value) -> QString {
            if (value.isEmpty()) {
                return tr("Prices cannot be empty");
            }
            const QStringList parts = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
            for (const QString &part : parts) {
                bool ok = false;
                const double v = part.toDouble(&ok);
                if (!ok || v < 0.0) {
                    return tr("Each price must be a valid non-negative number; got: %1").arg(part);
                }
            }
            return {};
        }
    };

    // ID_SIZES_FR — optional converted French/EU size numbers.
    // Each segment must be numeric when present.
    *attributes << Attribute{
        ID_SIZES_FR,
        tr("FR Sizes"),
        tr("Converted French/EU size numbers, separated by ; (optional)"),
        tr("34;36;38;40;42"),
        QString{},
        [](const QString &value) -> QString {
            if (value.isEmpty()) {
                return {}; // optional
            }
            const QStringList parts = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
            for (const QString &part : parts) {
                bool ok = false;
                part.toDouble(&ok);
                if (!ok) {
                    return tr("Each FR size must be a number; got: %1").arg(part);
                }
            }
            return {};
        },
        std::nullopt,
        true // optional
    };

    // ID_COLORS — product colour(s), optional.
    *attributes << Attribute{
        ID_COLORS,
        tr("Colors"),
        tr("Product colour(s), separated by ; (optional)"),
        tr("Blush Pink"),
        QString{},
        [](const QString &value) -> QString {
            if (value.isEmpty()) {
                return {}; // optional
            }
            const QStringList parts = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
            for (const QString &part : parts) {
                if (part.trimmed().isEmpty()) {
                    return tr("Colour values cannot be blank");
                }
            }
            return {};
        },
        std::nullopt,
        true // optional
    };

    // ID_SEXE — constrained to SEXE_MEN / SEXE_WOMEN / SEXE_UNISEX.
    *attributes << Attribute{
        ID_SEXE,
        tr("Sexe"),
        tr("Target gender: %1, %2, or %3").arg(SEXE_MEN, SEXE_WOMEN, SEXE_UNISEX),
        SEXE_WOMEN,
        QString{},
        [](const QString &value) -> QString {
            if (value != SEXE_MEN && value != SEXE_WOMEN && value != SEXE_UNISEX) {
                return tr("Sexe must be one of: %1, %2, %3").arg(SEXE_MEN, SEXE_WOMEN, SEXE_UNISEX);
            }
            return {};
        }
    };

    // ID_AGE — constrained to AGE_ADULT / AGE_KID / AGE_BABY.
    *attributes << Attribute{
        ID_AGE,
        tr("Age"),
        tr("Target age group: %1, %2, or %3").arg(AGE_ADULT, AGE_KID, AGE_BABY),
        AGE_ADULT,
        QString{},
        [](const QString &value) -> QString {
            if (value != AGE_ADULT && value != AGE_KID && value != AGE_BABY) {
                return tr("Age must be one of: %1, %2, %3").arg(AGE_ADULT, AGE_KID, AGE_BABY);
            }
            return {};
        }
    };

    // ID_TYPE — constrained to TYPE_CLOTHE / TYPE_SHOES / TYPE_ACCESSORY.
    *attributes << Attribute{
        ID_TYPE,
        tr("Type"),
        tr("Product type: %1, %2, or %3").arg(TYPE_CLOTHE, TYPE_SHOES, TYPE_ACCESSORY),
        TYPE_CLOTHE,
        QString{},
        [](const QString &value) -> QString {
            if (value != TYPE_CLOTHE && value != TYPE_SHOES && value != TYPE_ACCESSORY) {
                return tr("Type must be one of: %1, %2, %3").arg(TYPE_CLOTHE, TYPE_SHOES, TYPE_ACCESSORY);
            }
            return {};
        }
    };

    // ID_MATERIAL — optional fabric/composition string.
    *attributes << Attribute{
        ID_MATERIAL,
        tr("Material"),
        tr("Fabric/material composition (optional), e.g. 20% spandex 80% polyester"),
        tr("20% spandex 80% polyester"),
        QString{},
        [](const QString &) -> QString { return {}; },
        std::nullopt,
        true // optional
    };

    return attributes;
}

// ---------------------------------------------------------------------------
// Cross-validation
// ---------------------------------------------------------------------------

QString PageAttributesProductFashion::areAttributesCrossValid(
    const QHash<QString, QString> &id_values) const
{
    const QString sizes  = id_values.value(ID_SIZES);
    const QString prices = id_values.value(ID_PRICES);

    const int sizesCount  = sizes.isEmpty()  ? 0 : sizes.split(QLatin1Char(';'),  Qt::SkipEmptyParts).size();
    const int pricesCount = prices.isEmpty() ? 0 : prices.split(QLatin1Char(';'), Qt::SkipEmptyParts).size();

    if (sizesCount != pricesCount) {
        return tr("Sizes and Prices must have the same number of values (%1 vs %2)")
                .arg(QString::number(sizesCount), QString::number(pricesCount));
    }

    return {};
}
