#ifndef COMMONBLOCMENUTOP_H
#define COMMONBLOCMENUTOP_H

#include "website/commonblocs/AbstractCommonBlocMenu.h"

/**
 * Common bloc that renders the top (primary) navigation menu.
 *
 * Items are ordered by their index in m_items.  Supports Global, PerTheme,
 * and PerDomain scopes.
 *
 * addCode() renders a responsive, mobile-first navigation:
 *  - Mobile (< 768 px): hamburger button reveals a stacked list.  Items with
 *    children expose a click-toggle submenu.
 *  - Desktop (>= 768 px): horizontal list; items with children open a dropdown
 *    on hover / focus-within.
 *
 * BEM class names (.nav-top, .nav-top__hamburger, .nav-top__list, …) let the
 * theme CSS override colours and spacing without touching the structure.
 * ARIA attributes (role="navigation", aria-expanded, aria-haspopup, …) and
 * rel="noopener noreferrer" on target="_blank" links follow Google's
 * accessibility and security guidelines.
 *
 * CSS and JS are guarded by cssDoneIds / jsDoneIds and emitted at most once
 * per page.
 */
class CommonBlocMenuTop : public AbstractCommonBlocMenu
{
public:
    static constexpr const char *ID = "menu_top";

    CommonBlocMenuTop() = default;
    ~CommonBlocMenuTop() override = default;

    QString          getId()           const override;
    QString          getName()         const override;
    QList<ScopeType> supportedScopes() const override;

    void addCode(QStringView     origContent,
                 AbstractEngine &engine,
                 int             websiteIndex,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;
};

#endif // COMMONBLOCMENUTOP_H
