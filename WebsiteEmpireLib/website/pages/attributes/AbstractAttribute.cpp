#include "AbstractAttribute.h"

AbstractAttribute::AbstractAttribute(int id, const QString &name)
    : m_id(id)
    , m_name(name)
{
}

int AbstractAttribute::id() const
{
    return m_id;
}

const QString &AbstractAttribute::name() const
{
    return m_name;
}
