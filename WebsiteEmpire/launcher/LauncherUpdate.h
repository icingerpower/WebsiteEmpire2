#ifndef LAUNCHERUPDATE_H
#define LAUNCHERUPDATE_H

#include "AbstractLauncher.h"

/**
 * Headless launcher that applies AI update prompts to already-generated pages.
 *
 * Triggered by:
 *   WebsiteEmpire --workingDir <dir> --update [--strategy UUID] [--prompt UUID] [--limit N]
 *
 * --strategy UUID  Run only the update strategy with this UUID.  Default: all strategies.
 * --prompt   UUID  Run only the prompt step with this UUID within the strategy.
 *                  Default: all prompts, round-robin (each page is updated by each prompt).
 * --limit    N     Maximum total pages to update.  Default: unlimited.
 *                  PaneUpdate passes --limit 1 for its "Update one" button.
 *
 * For each qualifying (strategy, prompt) pair the launcher calls findPagesForUpdate()
 * which returns pages ordered so that those never updated by this prompt come first,
 * followed by those updated least recently.  This prevents always re-updating the
 * same pages.
 *
 * The update flow per page:
 *   1. Load existing 1_text from page_data.
 *   2. Build update prompt: existing text + prompt instructions.
 *   3. Run the Claude CLI.
 *   4. Validate response (must start with [TITLE level="1"] and be ≥ 2000 chars).
 *   5. Replace 1_text in the page_data map and call saveData().
 *   6. Call recordUpdateAttempt(pageId, promptId).
 */
class LauncherUpdate : public AbstractLauncher
{
public:
    static const QString OPTION_NAME;
    static const QString OPTION_STRATEGY;
    static const QString OPTION_PROMPT;
    static const QString OPTION_LIMIT;

    QString getOptionName() const override;
    bool    isFlag()        const override; // true — no value after --update

    void run(const QString &value) override;
};

#endif // LAUNCHERUPDATE_H
