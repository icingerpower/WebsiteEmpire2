#ifndef PARAM_H
#define PARAM_H

#include <QString>
#include <QVariant>

/**
 * A configurable parameter exposed by an AbstractTheme.
 *
 * Parameters are displayed as rows in the theme's QAbstractTableModel,
 * letting the user override default values per theme instance via a
 * QTableView.  Common blocs read param values at generation time through
 * AbstractTheme::paramValue().
 *
 * - id           Stable key used by common blocs to look up values.
 *                Never change once data exists on disk.
 * - name         Human-readable display name; must be wrapped in tr() at
 *                the call site (inside AbstractTheme::getParams()).
 * - tooltip      Explanation shown as a tooltip in the editor table;
 *                must be wrapped in tr() at the call site.
 * - defaultValue Fallback used when the user has not saved an override.
 */
struct Param {
    QString  id;
    QString  name;
    QString  tooltip;
    QVariant defaultValue;
};

#endif // PARAM_H
