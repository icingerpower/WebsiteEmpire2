#ifndef ATTRIBUTECATEGORY_H
#define ATTRIBUTECATEGORY_H

#include "website/pages/attributes/AbstractAttribute.h"

/**
 * A tag or category label (e.g. "yoga", "detox").
 * The name IS the category; no extra data is required beyond the base class.
 */
class AttributeCategory : public AbstractAttribute
{
public:
    AttributeCategory(int id, const QString &name);
    Type getType() const override;
};

#endif // ATTRIBUTECATEGORY_H
