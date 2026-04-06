#ifndef MENUITEM_H
#define MENUITEM_H

#include <QList>
#include <QString>

/**
 * A link entry used as a sub-row inside a MenuItem.
 * Sub-items cannot have their own children — one level of nesting maximum.
 */
struct MenuSubItem {
    QString label;
    QString url;
    QString rel;       ///< space-separated rel tokens, e.g. "nofollow noopener"; empty = no attr
    bool    newTab = false;
};

/**
 * A top-level entry in a menu common bloc.
 * Position is implicit from the item's index in the QList.
 *
 * If newTab is true, addCode() automatically appends "noopener noreferrer" to
 * the rel attribute (Google / security best practice) even if rel is empty.
 */
struct MenuItem {
    QString            label;
    QString            url;
    QString            rel;
    bool               newTab   = false;
    QList<MenuSubItem> children; ///< Optional sub-menu entries; they cannot nest further
};

#endif // MENUITEM_H
