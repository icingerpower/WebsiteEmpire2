#include "TaxonomySettings.h"

#include <QSettings>

TaxonomySettings::TaxonomySettings(const QDir &workingDir)
    : m_path(workingDir.filePath(QStringLiteral("taxonomy_settings.ini")))
{
}

QString TaxonomySettings::sourcePath(const QString &type) const
{
    QSettings s(m_path, QSettings::IniFormat);
    s.beginGroup(type);
    const QString val = s.value(QStringLiteral("source_path")).toString();
    s.endGroup();
    return val;
}

void TaxonomySettings::setSourcePath(const QString &type, const QString &path)
{
    QSettings s(m_path, QSettings::IniFormat);
    s.beginGroup(type);
    s.setValue(QStringLiteral("source_path"), path);
    s.endGroup();
}

QString TaxonomySettings::lastSync(const QString &type) const
{
    QSettings s(m_path, QSettings::IniFormat);
    s.beginGroup(type);
    const QString val = s.value(QStringLiteral("last_sync")).toString();
    s.endGroup();
    return val;
}

void TaxonomySettings::setLastSync(const QString &type, const QString &isoDateTime)
{
    QSettings s(m_path, QSettings::IniFormat);
    s.beginGroup(type);
    s.setValue(QStringLiteral("last_sync"), isoDateTime);
    s.endGroup();
}

int TaxonomySettings::itemCount(const QString &type) const
{
    QSettings s(m_path, QSettings::IniFormat);
    s.beginGroup(type);
    const int val = s.value(QStringLiteral("item_count"), 0).toInt();
    s.endGroup();
    return val;
}

void TaxonomySettings::setItemCount(const QString &type, int count)
{
    QSettings s(m_path, QSettings::IniFormat);
    s.beginGroup(type);
    s.setValue(QStringLiteral("item_count"), count);
    s.endGroup();
}
