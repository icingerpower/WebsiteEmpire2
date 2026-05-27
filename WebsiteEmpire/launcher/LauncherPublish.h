#ifndef LAUNCHERPUBLISH_H
#define LAUNCHERPUBLISH_H

#include "AbstractLauncher.h"

/**
 * Headless launcher that generates all pages and publishes them locally,
 * one sub-directory per language.
 *
 * Triggered by:
 *   WebsiteEmpire --workingDir <dir> --publish [--deploy <path>] [--port N]
 *
 * --deploy <path>   Base deploy folder.  Each qualifying language is placed in
 *                   <path>/<lang>/ (e.g. deploy/en/, deploy/fr/).
 *                   Defaults to <workingDir>/deploy.
 * --port   N        Port for the first deployed language (default 8080).
 *                   Subsequent languages use N+1, N+2, … in engine-row order.
 *
 * A language qualifies for deployment when at least 5 pages in pages.db are
 * written in that language (lang == code) or translated to it
 * (code in langCodesToTranslate).
 *
 * Steps per qualifying language:
 *   1. Load the engine from settings.ini (engineId key).
 *   2. Sync category hub stubs and mark stale hubs.
 *   3. Call PageGenerator::generateAll() for each qualifying language.
 *   4. Clear the hub dirty set and mark all complete pages as published.
 *   5. Extract that language's pages from the shared content.db into
 *      <deployPath>/<lang>/content.db.
 *   6. Regenerate sitemap and robots.txt for that language-specific DB.
 *   7. Copy images.db → <deployPath>/<lang>/images.db.
 *   8. Kill any StaticWebsiteServe running from that directory and restart
 *      with --port <assigned port>.
 */
class LauncherPublish : public AbstractLauncher
{
public:
    static const QString OPTION_NAME;
    static const QString OPTION_DEPLOY;
    static const QString OPTION_PORT;

    QString getOptionName() const override;
    bool    isFlag()        const override; // true — no value after --publish

    void run(const QString &value) override;
};

#endif // LAUNCHERPUBLISH_H
