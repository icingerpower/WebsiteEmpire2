#ifndef ABSTRACTPAGEBLOC_H
#define ABSTRACTPAGEBLOC_H

#include "website/WebCodeAdder.h"

#include <QHash>
#include <QList>
#include <QString>

class AbstractPageBlockWidget;
class AbstractAttribute;

/**
 * Base class for a page bloc: a self-contained unit that can both contribute
 * HTML/CSS/JS to a generated page (via WebCodeAdder::addCode) and provide a
 * Qt widget for editing its content in the IDE (via createEditWidget).
 *
 * Ownership of the returned widget is transferred to the caller.
 */
class AbstractPageBloc : public WebCodeAdder
{
public:
    virtual ~AbstractPageBloc() = default;

    /**
     * Load this bloc's content from a flat key→value map.
     * Keys not recognised by this bloc are silently ignored (forward
     * compatibility: old data may contain keys for attributes that were
     * later removed from the bloc's definition).
     */
    virtual void load(const QHash<QString, QString> &values) = 0;

    /**
     * Save this bloc's content into a flat key→value map.
     * Keys must be stable identifiers — never change them once data exists
     * in the database.
     */
    virtual void save(QHash<QString, QString> &values) const = 0;

    /**
     * Create and return a widget that lets the user edit this bloc's content.
     * Ownership is transferred to the caller.
     */
    virtual AbstractPageBlockWidget *createEditWidget() = 0;

    /**
     * Returns the semantic attributes this bloc exposes for indexing and
     * faceted search (categories, properties, qualities).
     *
     * The returned list contains raw pointers to static const instances owned
     * by the concrete subclass — no allocation, no ownership transfer.
     * The default implementation returns a reference to a static empty list.
     * Subclasses that expose attributes must store the list as a member and
     * return a const reference to it to avoid per-call copies.
     */
    virtual const QList<const AbstractAttribute *> &getAttributes() const;
};

#endif // ABSTRACTPAGEBLOC_H
