#ifndef STATSDBDATASOURCE_H
#define STATSDBDATASOURCE_H

#include "AbstractPerformanceDataSource.h"

#include <QDir>

/**
 * Performance data source backed by the local stats.db visit log written by
 * the Drogon serving stack (StaticWebsiteServe).
 *
 * stats.db must be present in the working directory (typically rsync'd from
 * the server after each deployment).  If the file is absent, isConfigured()
 * returns false and fetchData() returns an empty list — GenScheduler then
 * falls back to even distribution.
 *
 * Because stats.db only records actual visits (no impression data), both
 * UrlPerformance::impressions and UrlPerformance::clicks are set to the visit
 * count.  The weighting result is proportional to real traffic, which is a
 * reasonable proxy when GSC data is unavailable.
 */
class StatsDbDataSource : public AbstractPerformanceDataSource
{
public:
    explicit StatsDbDataSource(const QDir &workingDir);

    bool isConfigured() const override;

    QList<UrlPerformance> fetchData(const QString &domain, int nDays = 30) override;

private:
    QString m_dbPath;
};

#endif // STATSDBDATASOURCE_H
