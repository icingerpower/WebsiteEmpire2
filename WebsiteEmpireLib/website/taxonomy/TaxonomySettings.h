#ifndef TAXONOMYSETTINGS_H
#define TAXONOMYSETTINGS_H
#include <QDir>
#include <QString>

/**
 * Persists per-taxonomy metadata in <workingDir>/taxonomy_settings.ini.
 * Stores: source DB path, last sync timestamp, item count.
 * Uses QSettings with IniFormat.
 */
class TaxonomySettings
{
public:
    explicit TaxonomySettings(const QDir &workingDir);

    QString sourcePath(const QString &type) const;
    void    setSourcePath(const QString &type, const QString &path);

    QString lastSync(const QString &type) const;   // ISO datetime, empty if never
    void    setLastSync(const QString &type, const QString &isoDateTime);

    int     itemCount(const QString &type) const;
    void    setItemCount(const QString &type, int count);

private:
    QString m_path;
};
#endif
