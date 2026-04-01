#ifndef ABSTRACTPAGEBLOC_H
#define ABSTRACTPAGEBLOC_H

#include "website/WebCodeAdder.h"

class AbstractPageBlockWidget;

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
};

#endif // ABSTRACTPAGEBLOC_H
