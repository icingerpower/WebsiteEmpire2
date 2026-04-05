#ifndef CATEGORYTABLE_H
#define CATEGORYTABLE_H

#include <QDir>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

/**
 * Persistence class for the application's global category vocabulary.
 *
 * Loads and saves categories.csv from the working directory.
 * Each row has a stable int id, an optional parentId (0 = root), an English
 * canonical name, and per-language-code translations used during website
 * generation.
 *
 * CSV format:
 *   Id;ParentId;Name;<langCode1>;<langCode2>;...
 * Language-code columns are discovered dynamically from the header so that
 * adding translations for a new language only requires adding a column.
 *
 * IDs are positive integers, auto-incremented from the current maximum.
 * A removed category's id is never reused.
 */
class CategoryTable : public QObject
{
    Q_OBJECT

public:
    struct CategoryRow {
        int     id       = 0;
        int     parentId = 0; // 0 = root
        QString name;
        QMap<QString, QString> translations; // langCode → translated name
    };

    explicit CategoryTable(const QDir &workingDir, QObject *parent = nullptr);

    // --- Queries ---------------------------------------------------------------

    const QList<CategoryRow> &categories() const;

    // Returns nullptr if id is not found.
    const CategoryRow *categoryById(int id) const;

    // Returns base if no existing category has that name, otherwise appends
    // -2, -3, … until the name is unique.  Pass excludeId to skip the
    // category being renamed (so renaming to the same name is a no-op).
    QString uniqueName(const QString &base, int excludeId = -1) const;

    // Returns the translation for id in langCode, falling back to the English
    // name if no translation exists for that language.
    QString translationFor(int id, const QString &langCode) const;

    // --- Mutations (each saves immediately to disk) ----------------------------

    // Appends a new category; parentId 0 means root.  Returns the new stable id.
    int  addCategory(const QString &name, int parentId = 0);

    // Removes the category and all its descendants.
    void removeCategory(int id);

    // Renames the English canonical name for the given category.
    void renameCategory(int id, const QString &newName);

    // Sets or overwrites a single translation.
    void setTranslation(int id, const QString &langCode, const QString &value);

signals:
    void categoriesReset();
    void categoryAdded(int id);
    // Emits the full set of removed ids (the root and all its descendants) so
    // that subscribers holding references to category ids can clean up in one
    // pass without needing to re-query the table (the rows are already gone).
    void categoryRemoved(QList<int> allRemovedIds);
    void categoryRenamed(int id);

private:
    void _load();
    void _save();

    QString           m_filePath;
    QList<CategoryRow> m_rows;
};

#endif // CATEGORYTABLE_H
