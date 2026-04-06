#ifndef ABSTRACTCOMMONBLOCMENU_H
#define ABSTRACTCOMMONBLOCMENU_H

#include "website/commonblocs/AbstractCommonBloc.h"
#include "website/commonblocs/MenuItem.h"

#include <QList>
#include <QString>

/**
 * Base class for menu common blocs (top and bottom navigation).
 *
 * Holds the ordered item list and provides:
 *  - setItems() / items() for the repository to push data before addCode()
 *  - createEditWidget() — returns a WidgetCommonBlocMenu (same widget for
 *    both top and bottom menus, since the editing UI is identical)
 *  - buildRel() static helper — appends "noopener noreferrer" when newTab is
 *    true and rel does not already contain "noopener", as required by Google /
 *    security best practices.
 *
 * Subclasses implement getId(), getName(), supportedScopes(), and addCode().
 */
class AbstractCommonBlocMenu : public AbstractCommonBloc
{
public:
    /** Replace the full item list. */
    void setItems(const QList<MenuItem> &items);

    /** Read-only access to the current item list. */
    const QList<MenuItem> &items() const;

    /** Returns a new WidgetCommonBlocMenu. Caller takes ownership. */
    AbstractCommonBlocWidget *createEditWidget() override;

protected:
    /**
     * Builds the final rel attribute value for an anchor.
     *
     * If newTab is true and rel does not already contain "noopener",
     * "noopener noreferrer" is appended (Google / security guideline).
     * Returns rel unchanged when newTab is false.
     */
    static QString buildRel(const QString &rel, bool newTab);

    QList<MenuItem> m_items;
};

#endif // ABSTRACTCOMMONBLOCMENU_H
