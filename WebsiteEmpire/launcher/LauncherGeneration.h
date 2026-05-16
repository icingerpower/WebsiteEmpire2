#ifndef LAUNCHERGENERATION_H
#define LAUNCHERGENERATION_H

#include "AbstractLauncher.h"

/**
 * Headless launcher that generates AI content for pending source pages.
 *
 * Triggered by:
 *   WebsiteEmpire --workingDir <dir> --generation [--sessions N] [--limit N]
 *
 * --sessions  N      Number of parallel Claude API sessions (1-10, default 1).
 * --limit     N      Maximum total pages to generate across all strategies before
 *                    stopping (useful for spot-checks: PaneGeneration uses --limit 1
 *                    for its "Generate one" button).  Default: unlimited.
 * --strategy  ID     Run only the strategy with this UUID.  Default: all priority-1
 *                    strategies.  PaneGeneration passes this when "View gen. command"
 *                    is clicked so the terminal command targets a specific strategy.
 * --topic    "name"  GUI-only: bypass the source DB and generate exactly one page
 *                    for the given topic name.  Requires --strategy.  The topic is
 *                    slugified and the strategy's endPermalink suffix is appended as
 *                    usual.  Generation is skipped if the permalink already exists.
 * --delay     N      Seconds to wait between pages (default 0).  Use 20-30 when
 *                    generating many pages to avoid Claude API rate-limit errors.
 * --retry-only       Skip all new pending items (aspire DB and Pending state pages).
 *                    Only processes the retry queue (ContentReady SVG retries and
 *                    SocialMedia-flagged MainImageReady pages).  Used by the GUI's
 *                    "Generate phase 2" button so the launcher never picks up new
 *                    articles alongside the selected phase-2 pages.
 *
 * The launcher reads the engine id from settings.ini, loads GenStrategyTable,
 * uses GenScheduler to weight strategies, and runs one GenPageQueue per active
 * strategy.  Each session calls the Claude CLI (claude -p) and waits for it to
 * finish with no timeout — press Ctrl+C to stop gracefully after the current
 * pages finish.
 *
 * When gsc_settings.json is present in the working directory, GscDataSource is
 * used for priority weighting; otherwise StatsDbDataSource is tried; if both
 * are unavailable, even distribution is applied.
 */
class LauncherGeneration : public AbstractLauncher
{
public:
    static const QString OPTION_NAME;
    static const QString OPTION_SESSIONS;
    static const QString OPTION_LIMIT;
    static const QString OPTION_STRATEGY;
    static const QString OPTION_TOPIC;
    static const QString OPTION_NEW_ONLY;
    static const QString OPTION_RETRY_ONLY;
    static const QString OPTION_DELAY;

    QString getOptionName() const override;
    bool    isFlag()        const override; // true — no value after --generation

    void run(const QString &value) override;
};

#endif // LAUNCHERGENERATION_H
