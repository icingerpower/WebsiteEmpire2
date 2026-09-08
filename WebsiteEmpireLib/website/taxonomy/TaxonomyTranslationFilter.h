#ifndef TAXONOMYTRANSLATIONFILTER_H
#define TAXONOMYTRANSLATIONFILTER_H

#include <QStringList>

class AbstractEngine;

/**
 * Narrows a list of taxonomy type ids (e.g. from TaxonomyDb::allTypes()) down
 * to only those an engine's page types actually declare as translatable.
 *
 * Used by LauncherTranslateCommon so --translateCommon only ever translates
 * taxonomies that opted in (TaxonomyDescriptor::translatable == true) — e.g.
 * a Fashion engine's Color/Season/Occasion/Material/Style taxonomies, never
 * a Health engine's Symptoms taxonomy, regardless of what happens to be
 * present in taxonomy.db.
 */
namespace TaxonomyTranslationFilter {

/**
 * Returns the subset of allTypes for which engine declares (via some page
 * type's bloc's AbstractPageBloc::taxonomies()) a TaxonomyDescriptor with
 * that id and translatable == true. Order follows allTypes. Returns an
 * empty list when engine is null.
 */
QStringList filterTranslatable(const AbstractEngine *engine, const QStringList &allTypes);

} // namespace TaxonomyTranslationFilter

#endif // TAXONOMYTRANSLATIONFILTER_H
