#include "GenScheduler.h"

#include "IPageRepository.h"
#include "website/perf/AbstractPerformanceDataSource.h"

#include <algorithm>

GenScheduler::GenScheduler(const QList<StrategyInfo>     &strategies,
                            IPageRepository               &pageRepo,
                            AbstractPerformanceDataSource *dataSource,
                            const QString                 &domain)
    : m_strategies(strategies)
    , m_pageRepo(pageRepo)
    , m_dataSource(dataSource)
    , m_domain(domain)
{
}

QList<GenScheduler::StrategyAllocation> GenScheduler::computeAllocations(int totalSessions)
{
    // ---- Collect active strategies (those with pending pages) --------------
    struct Candidate {
        StrategyInfo info;
        int          pending = 0;
        double       weight  = 1.0;
    };

    QList<Candidate> candidates;

    for (const StrategyInfo &info : std::as_const(m_strategies)) {
        const int pending = (info.pendingCountOverride >= 0)
                            ? info.pendingCountOverride
                            : m_pageRepo.findPendingByTypeId(info.pageTypeId).size();
        if (pending == 0) {
            continue;
        }
        Candidate c;
        c.info    = info;
        c.pending = pending;
        c.weight  = 1.0;
        candidates.append(c);
    }

    if (candidates.isEmpty()) {
        return {};
    }

    // ---- Apply performance weights if data is available -------------------
    if (m_dataSource && m_dataSource->isConfigured()) {
        const QList<UrlPerformance> perf = m_dataSource->fetchData(m_domain);

        if (!perf.isEmpty()) {
            // Sum impressions by page type by cross-referencing URLs with the
            // page repository.  findAll() is called once for an in-memory match.
            // This runs before coroutines start so blocking here is acceptable.
            const QList<PageRecord> all = m_pageRepo.findAll();

            QHash<QString, int> impressionsByType;
            for (const UrlPerformance &entry : std::as_const(perf)) {
                for (const PageRecord &rec : std::as_const(all)) {
                    if (rec.permalink == entry.url) {
                        impressionsByType[rec.typeId] += entry.impressions;
                        break;
                    }
                }
            }

            if (!impressionsByType.isEmpty()) {
                for (auto &c : candidates) {
                    const int imp = impressionsByType.value(c.info.pageTypeId, 0);
                    c.weight = (imp > 0) ? static_cast<double>(imp) : 1.0;
                }
            }
        }
    }

    // ---- Cap candidates to totalSessions ----------------------------------------
    // Each active strategy gets at least 1 session, so we can run at most
    // totalSessions strategies concurrently.  When more candidates exist than
    // available sessions, keep only the highest-weight ones (stable sort preserves
    // original order for equal weights, so the first-listed strategy wins ties).
    if (static_cast<int>(candidates.size()) > totalSessions) {
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const Candidate &a, const Candidate &b) {
                             return a.weight > b.weight;
                         });
        candidates.resize(totalSessions);
    }

    // ---- Distribute sessions proportionally --------------------------------
    const double totalWeight = [&]() {
        double sum = 0.0;
        for (const auto &c : std::as_const(candidates)) { sum += c.weight; }
        return sum;
    }();

    QList<StrategyAllocation> result;
    result.reserve(candidates.size());

    int sessionsAssigned = 0;

    for (const auto &c : std::as_const(candidates)) {
        StrategyAllocation alloc;
        alloc.strategyId         = c.info.strategyId;
        alloc.pageTypeId         = c.info.pageTypeId;
        alloc.primaryAttrId      = c.info.primaryAttrId;
        alloc.customInstructions = c.info.customInstructions;
        alloc.nonSvgImages       = c.info.nonSvgImages;
        alloc.sessionCount = qMax(1, qRound(totalSessions * c.weight / totalWeight));
        sessionsAssigned  += alloc.sessionCount;
        result.append(alloc);
    }

    // Trim excess sessions from the highest-count strategy if we over-assigned.
    while (sessionsAssigned > totalSessions && !result.isEmpty()) {
        auto it = std::max_element(result.begin(), result.end(),
                                   [](const StrategyAllocation &a,
                                      const StrategyAllocation &b) {
                                       return a.sessionCount < b.sessionCount;
                                   });
        if (it->sessionCount > 1) {
            --(it->sessionCount);
            --sessionsAssigned;
        } else {
            break; // all at minimum — stop trimming
        }
    }

    return result;
}
