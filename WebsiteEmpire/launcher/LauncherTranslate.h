#ifndef LAUNCHERTRANSLATE_H
#define LAUNCHERTRANSLATE_H

#include "AbstractLauncher.h"

// Headless launcher that runs PageTranslator from the CLI.
//
// Triggered by: WebsiteEmpire --workingDir <dir> --translate
//
// Reads the engine id from settings.ini and the editing language from
// settings_global.csv, then translates all pending source pages to every
// target language declared by the engine.  Exits when translation is complete
// (or on the first fatal error).
//
// ANTHROPIC_API_KEY must be set in the environment.
class LauncherTranslate : public AbstractLauncher
{
public:
    // Option name (without "--") used to trigger this launcher.
    // Exposed as a static constant so PanePages can reference it without
    // needing an instance.
    static const QString OPTION_NAME;

    QString getOptionName() const override;

    // translate is a flag — no value is needed on the command line.
    bool isFlag() const override;

    void run(const QString &value) override;
};

#endif // LAUNCHERTRANSLATE_H
