#ifndef LAUNCHERTRANSLATE_H
#define LAUNCHERTRANSLATE_H

#include "AbstractLauncher.h"

// Headless launcher that runs PageTranslator from the CLI.
//
// Triggered by: WebsiteEmpire --workingDir <dir> --translate [--language <code>] [--limit <n>]
//
// Reads the engine id from settings.ini and the editing language from
// settings_global.csv, then translates all pending source pages to every
// target language declared by the engine.  Exits when translation is complete
// (or on the first fatal error).
//
// --language <code>  Optional filter: only translate into this BCP-47 language
//                    code (e.g. "fr", "de").  Useful for smoke-testing one
//                    language before running the full multi-language pass.
//
// --limit <n>        Optional cap: stop after at most n translation jobs in
//                    this run.  Overrides the limitPerRun value in
//                    translation_settings.json when both are set.
//
// ANTHROPIC_API_KEY must be set in the environment.
class LauncherTranslate : public AbstractLauncher
{
public:
    // Option names (without "--") used to trigger this launcher and its sub-options.
    // Exposed as static constants so PaneTranslations can reference them without
    // needing an instance.
    static const QString OPTION_NAME;
    static const QString OPTION_LANGUAGE;
    static const QString OPTION_LIMIT;

    QString getOptionName() const override;

    // translate is a flag — no value is needed on the command line.
    bool isFlag() const override;

    void run(const QString &value) override;
};

#endif // LAUNCHERTRANSLATE_H
