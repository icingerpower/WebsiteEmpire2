#ifndef ATTRIBUTECATEGORY_H
#define ATTRIBUTECATEGORY_H

#include "website/pages/attributes/AbstractAttribute.h"

/**
 * A tag or category label (e.g. "yoga", "detox").
 *
 * Categories can be arranged in a parent/child hierarchy via parentId.
 * A parentId of 0 means the category is a root (no parent).
 * The parent is identified by its stable int id — the same key used for DB
 * columns and URL parameters — so no pointer is needed and there is no
 * static initialisation-order dependency between translation units.
 */
class AttributeCategory : public AbstractAttribute
{
public:
    AttributeCategory(int id, const QString &name, int parentId = 0);
    Type getType() const override;

    /** Returns the id of the parent category, or 0 if this is a root. */
    int parentId() const;

private:
    int m_parentId;
};

#endif // ATTRIBUTECATEGORY_H
