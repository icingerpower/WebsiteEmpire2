#ifndef VERTICALSYNCPOLICY_H
#define VERTICALSYNCPOLICY_H

#include <QString>

/**
 * Single source of truth for which vertical-specific hub syncers may run for a
 * given AbstractEngine::getGeneratorId().
 *
 * Exists because the rule had already been written out by hand in several
 * places (LauncherPublish, PaneDomains::deployLocally(), PaneGeneratedPages)
 * and immediately drifted: deployLocally() ran the Fashion tag-hub syncer AND
 * the Health symptom-hub syncer unconditionally, so a local deploy of either
 * site ran the other vertical's syncer. It produced nothing only because the
 * other vertical's source data happens to be absent from each working
 * directory — a Fashion site with any symptom data present would have grown
 * /symptoms pages, which is precisely the leakage the generator-id scoping was
 * introduced to prevent.
 *
 * The generator ids are the ones the engines return from getGeneratorId(),
 * which must match the corresponding AbstractGenerator::getId().
 */
namespace VerticalSyncPolicy {

/// GeneratorHealth::getId() — returned by EngineArticles::getGeneratorId().
inline constexpr const char *GENERATOR_HEALTH = "health";

/// GeneratorFashionTaxonomy::getId() — returned by EngineArticlesFashion.
inline constexpr const char *GENERATOR_FASHION_TAXONOMY = "fashion_taxonomy";

/**
 * True when the engine's vertical owns the Fashion taxonomy tag hubs, i.e.
 * FashionTaxonomyHubSyncer may create /style/…, /color/… stub pages.
 * False for every other generator id, including an empty one (an engine that
 * declares no generator must never grow another vertical's pages).
 */
bool needsFashionHubSync(const QString &generatorId);

/**
 * True when the engine's vertical owns the Health symptom hubs, i.e.
 * SymptomHubSyncer may create /symptoms/… stub pages.
 */
bool needsSymptomHubSync(const QString &generatorId);

} // namespace VerticalSyncPolicy

#endif // VERTICALSYNCPOLICY_H
