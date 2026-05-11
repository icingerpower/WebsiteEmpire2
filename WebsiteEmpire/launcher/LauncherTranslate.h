#ifndef LAUNCHERTRANSLATE_H
#define LAUNCHERTRANSLATE_H

#include "AbstractLauncher.h"

#include <QList>

// Headless launcher that runs PageTranslator from the CLI.
//
// Triggered by: WebsiteEmpire --workingDir <dir> --translate [--language <code>] [--limit <n>] [--text|--svg]
//
// Default (no --text / --svg): runs text jobs first, then SVG back-fill.
// --text              Run text-only jobs (no SVG back-fill).
// --svg               Run SVG back-fill only (no text jobs).
//
// --language <code>  Optional filter: only translate into this BCP-47 language
//                    code (e.g. "fr", "de").  Useful for smoke-testing one
//                    language before running the full multi-language pass.
//
// --limit <n>        Optional cap: stop after at most n jobs per phase.
//                    Overrides the limitPerRun value in
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
    // When present, only SVG translation jobs are run (back-fill for already-translated pages).
    static const QString OPTION_SVG;
    // When present, only text translation jobs are run (no SVG back-fill).
    static const QString OPTION_TEXT;

    // Describes one supported run mode for --translate.
    struct TranslateMode {
        QString flag;    // extra flag after --translate (empty = default text+SVG mode)
        QString title;   // one-line title shown as a # comment header
        QString detail;  // optional explanatory detail (newline-separated lines, may be empty)
    };

    // Place a ModeRecorder instance adjacent to each new OPTION constant in
    // LauncherTranslate.cpp. The View Commands dialog iterates translateModes()
    // and auto-includes any registered mode — no manual update needed.
    class ModeRecorder {
    public:
        explicit ModeRecorder(const TranslateMode &mode);
    };

    // Returns all modes in registration order (default first, then each flag mode).
    static const QList<TranslateMode> &translateModes();

    QString getOptionName() const override;

    // translate is a flag — no value is needed on the command line.
    bool isFlag() const override;

    void run(const QString &value) override;

private:
    static QList<TranslateMode> &getModes();
};

#endif // LAUNCHERTRANSLATE_H
