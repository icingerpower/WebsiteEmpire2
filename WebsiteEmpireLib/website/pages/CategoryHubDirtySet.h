#ifndef CATEGORYHUBBDIRTYSET_H
#define CATEGORYHUBBDIRTYSET_H

#include <QDir>
#include <QSet>

/**
 * Crash-safe persistent set of category-hub page IDs that need HTML re-rendering.
 *
 * Every mutating call (add, addAll, remove, clear) atomically overwrites the
 * backing JSON file via QSaveFile so the set survives a crash mid-generation run.
 *
 * File format: {"hubPageIds": [1, 2, 3]}
 *
 * Intended usage in renderDirtyHubs():
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
class CategoryHubDirtySet
{
public:
    static constexpr const char *FILENAME = "dirty_hub_pages.json";

    explicit CategoryHubDirtySet(const QDir &workingDir);

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

#endif // CATEGORYHUBBDIRTYSET_H
