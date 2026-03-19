#ifndef PAGEATTRIBUTESPRODUCTFASHION_H
#define PAGEATTRIBUTESPRODUCTFASHION_H

#include "PageAttributesProduct.h"

// PageAttributesProductFashion extends PageAttributesProduct with attributes
// specific to fashion items (clothing, shoes, accessories).
//
// Per-variant data (one value per size, semicolon-separated):
//   • ID_SIZES    — raw size labels from the product page, e.g.
//                   "US-2 | UK/AU-6 | EU/DE-32 | FR/ES-34;US-4 | ..."
//   • ID_PRICES   — selling price for each size variant, e.g. "29.99;34.99"
//
// Optional converted sizing:
//   • ID_SIZES_FR — French/EU size numbers derived from the raw labels, e.g.
//                   "34;36;38" (optional; empty when no pattern matched)
//
// Descriptive attributes:
//   • ID_COLORS   — product colour(s), semicolon-separated, e.g. "Black;Beige"
//                   (optional; 0 to several values)
//   • ID_SEXE     — SEXE_MEN / SEXE_WOMEN / SEXE_UNISEX
//   • ID_AGE      — AGE_ADULT / AGE_KID / AGE_BABY
//   • ID_TYPE     — TYPE_CLOTHE / TYPE_SHOES / TYPE_ACCESSORY
//   • ID_MATERIAL — fabric composition, e.g. "20% spandex 80% polyester"
//                   (optional)
//
// Cross-validation: ID_SIZES and ID_PRICES must carry the same number of
// semicolon-separated values.
class PageAttributesProductFashion : public PageAttributesProduct
{
    Q_OBJECT

public:
    // Attribute IDs
    static const QString ID_SIZES;
    static const QString ID_PRICES;
    static const QString ID_SIZES_FR;
    static const QString ID_COLORS;
    static const QString ID_SEXE;
    static const QString ID_AGE;
    static const QString ID_TYPE;
    static const QString ID_MATERIAL;

    // Constrained values for ID_SEXE
    static const QString SEXE_MEN;
    static const QString SEXE_WOMEN;
    static const QString SEXE_UNISEX;

    // Constrained values for ID_AGE
    static const QString AGE_ADULT;
    static const QString AGE_KID;
    static const QString AGE_BABY;

    // Constrained values for ID_TYPE
    static const QString TYPE_CLOTHE;
    static const QString TYPE_SHOES;
    static const QString TYPE_ACCESSORY;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;

    // Checks that ID_SIZES and ID_PRICES carry the same number of
    // semicolon-separated values.
    QString areAttributesCrossValid(
        const QHash<QString, QString> &id_values) const override;
};

#endif // PAGEATTRIBUTESPRODUCTFASHION_H
