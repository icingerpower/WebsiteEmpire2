#ifndef ABSTRACTPERFORMANCEDATASOURCE_H
#define ABSTRACTPERFORMANCEDATASOURCE_H

#include <QList>
#include <QString>

/**
 * Raw performance entry for a single URL as returned by a data source.
 * Impressions represent how often the URL appeared in search results;
 * clicks represent actual visits.  StatsDb sources may only populate
 * clicks (treated as both clicks and impressions).
 */
struct UrlPerformance {
    QString url;
    int     impressions = 0;
    int     clicks      = 0;
};

/**
 * Abstract base for performance data providers used by GenScheduler to weight
 * generation strategy priorities.
 *
 * Concrete implementations:
 *   GscDataSource      — Google Search Console Search Analytics API (primary)
 *   StatsDbDataSource  — local stats.db visit log (fallback / zero-config)
 *
 * GenScheduler falls back to even distribution when isConfigured() returns
 * false or when fetchData() returns an empty list.
 */
class AbstractPerformanceDataSource
{
public:
    virtual ~AbstractPerformanceDataSource() = default;

    /**
     * Returns true when the data source has enough configuration to make a
     * real request.  If false, GenScheduler skips the fetch entirely.
     */
    virtual bool isConfigured() const = 0;

    /**
     * Fetches performance data for the given domain over the last nDays days.
     * domain is a bare hostname, e.g. "example.com".
     * Implementations that need a different identifier (e.g. a GSC property
     * URL) resolve it internally from the domain.
     * Returns an empty list on error or when no data is available.
     */
    virtual QList<UrlPerformance> fetchData(const QString &domain, int nDays = 30) = 0;
};

#endif // ABSTRACTPERFORMANCEDATASOURCE_H
