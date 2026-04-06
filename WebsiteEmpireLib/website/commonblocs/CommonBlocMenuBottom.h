#ifndef COMMONBLOCMENUBOTTOM_H
#define COMMONBLOCMENUBOTTOM_H

#include "website/commonblocs/AbstractCommonBlocMenu.h"

/**
 * Common bloc that renders the bottom (secondary / footer) navigation menu.
 *
 * Supports Global scope only — one instance shared across all themes and
 * domains.  Typical use: privacy policy, terms of service, cookie policy links
 * displayed at the bottom of every page.
 *
 * addCode() renders a compact, centred, wrapping flex list with BEM classes
 * (.nav-bottom, .nav-bottom__list, .nav-bottom__link).  Sub-items are ignored
 * on the bottom menu (only top-level items are rendered).  No JS is emitted.
 */
class CommonBlocMenuBottom : public AbstractCommonBlocMenu
{
public:
    static constexpr const char *ID = "menu_bottom";

    CommonBlocMenuBottom() = default;
    ~CommonBlocMenuBottom() override = default;

    QString          getId()           const override;
    QString          getName()         const override;

    /**
     * Returns {Global} — the bottom menu is not customised per theme or domain.
     */
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

#endif // COMMONBLOCMENUBOTTOM_H
