#include "AttributeQuality.h"

AttributeQuality::AttributeQuality(int id,
                                   const QString &name,
                                   const QStringList &possibleValues)
    : AbstractAttribute(id, name)
    , m_possibleValues(possibleValues)
{
}

AbstractAttribute::Type AttributeQuality::getType() const
{
    return Type::Quality;
}

const QStringList &AttributeQuality::possibleValues() const
{
    return m_possibleValues;
}
