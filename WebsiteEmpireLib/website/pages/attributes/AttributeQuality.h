#ifndef ATTRIBUTEQUALITY_H
#define ATTRIBUTEQUALITY_H

#include "website/pages/attributes/AbstractAttribute.h"

#include <QStringList>

/**
 * A named enum choice from a fixed vocabulary
 * (e.g. energy — possible values: "ying", "yang", "neutral").
 *
 * possibleValues() lists the default English labels in the order they should
 * be presented to the user; the AI translates them for other languages.
 */
class AttributeQuality : public AbstractAttribute
{
public:
    AttributeQuality(int id, const QString &name, const QStringList &possibleValues);
    Type getType() const override;

    const QStringList &possibleValues() const;

private:
    QStringList m_possibleValues;
};

#endif // ATTRIBUTEQUALITY_H
