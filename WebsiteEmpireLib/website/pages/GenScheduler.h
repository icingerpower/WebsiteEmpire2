#ifndef GENSCHEDULER_H
#define GENSCHEDULER_H

#include <QList>
#include <QString>

class AbstractPerformanceDataSource;
class IPageRepository;

/**
 * Computes how many parallel Claude sessions to allocate to each active
 * generation strategy based on performance data.
 *
 * Algorithm
 * ---------
 * 1. Iterate strategies; skip those with no pending pages.
 * 2. If a performance data source is available and returns data for the
 *    domain, weight each strategy by its total GSC impressions (or visits).
 *    Strategies with no data or zero impressions receive a floor weight of 1
 *    so they always get at least one session.
 * 3. Distribute totalSessions proportionally.  Every active strategy gets a
 *    minimum of 1 session; any remainder goes to the highest-weight strategy.
 * 4. If no data source is configured or fetchData() returns empty, all active
 *    strategies receive equal session counts (even distribution).
 *
 * The caller owns IPageRepository and the data source pointer; both must
 * outlive this object.  The data source pointer may be null (no data).
 */
class GenScheduler
{
public:
    /** Flat descriptor for one row of GenStrategyTable, GUI-layer independent. */
    struct StrategyInfo {
        QString strategyId;         // stable UUID from GenStrategyTable
        QString pageTypeId;
        QString themeId;            // empty = all themes / no filter
        QString customInstructions; // empty = use generic prompt
        bool    nonSvgImages = false;
    };

    struct StrategyAllocation {
        QString strategyId;
        QString pageTypeId;
        bool    nonSvgImages = false;
        int     sessionCount = 1;
    };

    GenScheduler(const QList<StrategyInfo>     &strategies,
                 IPageRepository               &pageRepo,
                 AbstractPerformanceDataSource *dataSource, // nullable
                 const QString                 &domain);

    /**
     * Returns one StrategyAllocation per active strategy (pending > 0),
     * with sessionCount summing to at most totalSessions.
     * Returns an empty list when there is nothing left to generate.
     */
    QList<StrategyAllocation> computeAllocations(int totalSessions);

private:
    QList<StrategyInfo>            m_strategies;
    IPageRepository               &m_pageRepo;
    AbstractPerformanceDataSource *m_dataSource;
    QString                        m_domain;
};

#endif // GENSCHEDULER_H
