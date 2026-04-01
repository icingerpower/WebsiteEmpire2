#ifndef ABSTRACTATTRIBUTE_H
#define ABSTRACTATTRIBUTE_H

#include <QString>

/**
 * Base class for semantic attributes that a page bloc can expose for
 * indexing, faceted filtering, and multi-criteria search.
 *
 * Each attribute is identified by a stable int id — cheap to store in the
 * database and in URL query parameters.  The English name (and, for
 * subclasses, the unit or the possible values) is the default used for
 * display; the AI layer translates it when the target site is in another
 * language.
 *
 * Instances are intended to be static const members of concrete bloc classes.
 * AbstractPageBloc::getAttributes() returns raw pointers to those statics so
 * neither heap allocation nor ownership transfer is needed on each call.
 *
 * The id must never change once data has been persisted: it is the stable
 * key used for DB columns and URL parameters.
 */
class AbstractAttribute
{
public:
    enum class Type { Category, Property, Quality };

    AbstractAttribute(int id, const QString &name);
    virtual ~AbstractAttribute() = default;

    virtual Type getType() const = 0;

    int            id()   const;
    const QString &name() const;

private:
    int     m_id;
    QString m_name;
};

#endif // ABSTRACTATTRIBUTE_H
