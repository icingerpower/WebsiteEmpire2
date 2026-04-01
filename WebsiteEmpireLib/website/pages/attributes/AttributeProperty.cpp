#include "AttributeProperty.h"

AttributeProperty::AttributeProperty(int id, const QString &name, const QString &unit)
    : AbstractAttribute(id, name)
    , m_unit(unit)
{
}

AbstractAttribute::Type AttributeProperty::getType() const
{
    return Type::Property;
}

const QString &AttributeProperty::unit() const
{
    return m_unit;
}
