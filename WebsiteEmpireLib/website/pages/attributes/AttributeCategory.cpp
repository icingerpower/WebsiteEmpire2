#include "AttributeCategory.h"

AttributeCategory::AttributeCategory(int id, const QString &name, int parentId)
    : AbstractAttribute(id, name)
    , m_parentId(parentId)
{
}

int AttributeCategory::parentId() const
{
    return m_parentId;
}

AbstractAttribute::Type AttributeCategory::getType() const
{
    return Type::Category;
}
