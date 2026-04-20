#ifndef LAUNCHERTRANSLATECOMMON_H
#define LAUNCHERTRANSLATECOMMON_H

#include "AbstractLauncher.h"

// Headless launcher that runs CommonBlocTranslator from the CLI.
//
// Triggered by: WebsiteEmpire --workingDir <dir> --translateCommon
//
// Reads the engine id and theme id from settings.ini, loads the theme's common
// blocs, and translates every field that is missing a translation for any of
// the engine's configured target languages.  Exits when complete (or on a
// fatal error).
//
// Source language is taken from the theme's stored source_lang (set by
// MainWindow whenever a domain row is added); falls back to the engine's
// first row language when not yet saved.
//
// ANTHROPIC_API_KEY must be set in the environment.
class LauncherTranslateCommon : public AbstractLauncher
{
public:
    // Option name (without "--") used to trigger this launcher.
    static const QString OPTION_NAME;

    QString getOptionName() const override;
    bool    isFlag()        const override;
    void    run(const QString &value) override;
};

#endif // LAUNCHERTRANSLATECOMMON_H
