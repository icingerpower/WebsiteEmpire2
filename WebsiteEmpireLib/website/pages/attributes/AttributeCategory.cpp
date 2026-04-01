#include "AttributeCategory.h"

AttributeCategory::AttributeCategory(int id, const QString &name)
    : AbstractAttribute(id, name)
{
}

AbstractAttribute::Type AttributeCategory::getType() const
{
    return Type::Category;
}
