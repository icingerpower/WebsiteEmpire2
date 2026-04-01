#ifndef ATTRIBUTEPROPERTY_H
#define ATTRIBUTEPROPERTY_H

#include "website/pages/attributes/AbstractAttribute.h"

/**
 * A named quantity with a measurement unit (e.g. iron = 100 mg, calories = 250 kcal).
 * The unit string is the default English label; the AI translates it for other languages.
 */
class AttributeProperty : public AbstractAttribute
{
public:
    AttributeProperty(int id, const QString &name, const QString &unit);
    Type getType() const override;

    const QString &unit() const;

private:
    QString m_unit;
};

#endif // ATTRIBUTEPROPERTY_H
