#ifndef LAUNCHERREVIEW_H
#define LAUNCHERREVIEW_H

#include "AbstractLauncher.h"

/**
 * Headless launcher that runs all registered review actions against every
 * source page and applies automated flags based on performance data.
 *
 * Triggered by:
 *   WebsiteEmpire --workingDir <dir> --review
 *
 * For each source page the launcher:
 *  1. Looks up the page's display count from the active performance data source
 *     (GscDataSource if configured, otherwise StatsDbDataSource, otherwise 0).
 *  2. Calls AbstractReviewAction::run() for every registered review action.
 *  3. Logs changes made.
 *
 * Currently registered actions:
 *   ReviewActionSocialMedia — sets PageFlag::SocialMedia and resets
 *     generation_state to MainImageReady when displayCount >= threshold.
 *     LauncherGeneration then processes these pages on the next --generation run.
 */
class LauncherReview : public AbstractLauncher
{
public:
    static const QString OPTION_NAME;

    QString getOptionName() const override;
    bool    isFlag()        const override; // true — no value after --review

    void run(const QString &value) override;
};

#endif // LAUNCHERREVIEW_H
