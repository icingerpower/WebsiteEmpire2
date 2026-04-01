#ifndef ABSTRACTPAGETYPE_H
#define ABSTRACTPAGETYPE_H

#include "website/WebCodeAdder.h"

#include <QList>
#include <QSet>

class AbstractPageBloc;
class AbstractAttribute;

/**
 * Base class for a page type.
 *
 * A page type is a WebCodeAdder (it contributes HTML/CSS/JS to a generated
 * page) that is composed of one or more page blocs.
 *
 * getPageBlocs() must return a const reference to a member list so that
 * neither addCode() nor getAttributes() incur a list copy on every call.
 *
 * getAttributes() lazily aggregates the attributes of all blocs on the first
 * call and caches the result in m_cachedAttributes; subsequent calls are
 * zero-allocation.
 */
class AbstractPageType : public WebCodeAdder
{
public:
    virtual ~AbstractPageType() = default;

    /**
     * Returns the ordered list of blocs that compose this page type.
     * Implementations must store the list as a member and return a const ref.
     * Pointers are const: callers may only invoke const methods on the blocs.
     */
    virtual const QList<const AbstractPageBloc *> &getPageBlocs() const = 0;

    /**
     * Delegates addCode() to every bloc in order.
     * Each bloc appends to html/css/js as appropriate.
     */
    void addCode(QStringView origContent,
                 QString &html, QString &css, QString &js,
                 QSet<QString> &cssDoneIds, QSet<QString> &jsDoneIds) const override;

    /**
     * Returns the union of attributes from all blocs.
     * Built lazily on the first call; subsequent calls return a cached ref.
     * Pointers are raw references into each bloc's static storage — no
     * heap allocation beyond the list itself.
     */
    const QList<const AbstractAttribute *> &getAttributes() const;

private:
    mutable QList<const AbstractAttribute *> m_cachedAttributes;
    mutable bool m_attributesCached = false;
};

#endif // ABSTRACTPAGETYPE_H
