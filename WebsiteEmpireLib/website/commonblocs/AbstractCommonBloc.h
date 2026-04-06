#ifndef ABSTRACTCOMMONBLOC_H
#define ABSTRACTCOMMONBLOC_H

#include "website/WebCodeAdder.h"

#include <QList>
#include <QString>

class AbstractCommonBlocWidget;

/**
 * Base class for cross-page HTML fragments: header, footer, top menu,
 * bottom menu.
 *
 * Unlike AbstractPageBloc, common blocs are NOT called during per-page
 * generation.  addCode() is called once per (domain, lang) when the
 * fragment needs to be (re)generated — e.g. after an edit or a menu item
 * reorder.  The result is stored as a fragment in content.db and
 * prepended/appended to page bodies at serve time by Drogon.
 *
 * Because common blocs are not driven by source text, origContent in
 * addCode() is always empty.  All data comes from the bloc's own member
 * variables, loaded by the repository before generation.  Implementations
 * must call Q_UNUSED(origContent).
 *
 * Common blocs are owned by their parent AbstractTheme.
 */
class AbstractCommonBloc : public WebCodeAdder
{
public:
    /**
     * Scope levels at which a common bloc instance can be defined.
     * supportedScopes() returns the subset offered by this bloc type.
     *
     * Resolution order (most specific wins): PerDomain > PerTheme > Global.
     */
    enum class ScopeType {
        Global,    ///< One instance shared across all themes and domains
        PerTheme,  ///< One instance per theme; overrides Global
        PerDomain, ///< One instance per domain; overrides PerTheme and Global
    };

    virtual ~AbstractCommonBloc() = default;

    /**
     * Stable identifier persisted to the DB — never change once data exists.
     */
    virtual QString getId() const = 0;

    /**
     * Human-readable name shown in the UI.
     * Must be wrapped with QCoreApplication::translate() in implementations.
     */
    virtual QString getName() const = 0;

    /**
     * Returns the scope levels the UI should offer for this bloc type.
     * All types except CommonBlocMenuBottom return {Global, PerTheme, PerDomain}.
     * CommonBlocMenuBottom returns {Global} only — it is not customised per
     * theme or domain.
     */
    virtual QList<ScopeType> supportedScopes() const = 0;

    /**
     * Create and return an editor widget for this bloc.
     * Ownership is transferred to the caller.
     *
     * Returns nullptr until a concrete widget is implemented.
     */
    virtual AbstractCommonBlocWidget *createEditWidget() = 0;
};

#endif // ABSTRACTCOMMONBLOC_H
