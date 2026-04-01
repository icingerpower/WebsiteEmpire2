#ifndef ABSTRACTPAGEBLOC_H
#define ABSTRACTPAGEBLOC_H

#include "website/WebCodeAdder.h"

#include <QList>

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
     * The default implementation returns an empty list.
     */
    virtual QList<const AbstractAttribute *> getAttributes() const;
};

#endif // ABSTRACTPAGEBLOC_H
