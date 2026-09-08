#ifndef FASHIONHUBDIRTYSET_H
#define FASHIONHUBDIRTYSET_H

#include <QDir>
#include <QSet>

/**
 * Crash-safe persistent set of fashion-tag-hub page IDs that need HTML
 * re-rendering. Structurally identical to CategoryHubDirtySet, kept as a
 * separate class (own JSON file) rather than a generalization of it —
 * touching working, load-bearing crash-safety code for an unrelated
 * feature is not worth the risk for ~40 lines of duplication.
 *
 * Every mutating call (add, addAll, remove, clear) atomically overwrites the
 * backing JSON file via QSaveFile so the set survives a crash mid-generation run.
 *
 * File format: {"hubPageIds": [1, 2, 3]}
 *
 * Intended usage in FashionTaxonomyHubSyncer::renderDirtyHubs():
 *
 *   for (int id : dirtySet.all()) {
 *       generator.generateSubset({id}, ...);   // write to content.db
 *       repo.setGeneratedAt(id, now);           // stamp timestamp
 *       dirtySet.remove(id);                    // persisted to disk immediately
 *   }
 *
 * If the process crashes between generateSubset() and remove(), the id remains
 * in the file; on the next run that hub is simply re-rendered (idempotent).
 */
class FashionHubDirtySet
{
public:
    static constexpr const char *FILENAME = "dirty_fashion_hub_pages.json";

    explicit FashionHubDirtySet(const QDir &workingDir);

    void add(int hubPageId);
    void addAll(const QSet<int> &hubPageIds);
    void remove(int hubPageId);
    void clear();

    const QSet<int> &all() const;
    bool             isEmpty()          const;
    bool             contains(int hubPageId) const;

private:
    void _load();
    void _save() const;

    QString   m_filePath;
    QSet<int> m_ids;
};

#endif // FASHIONHUBDIRTYSET_H
