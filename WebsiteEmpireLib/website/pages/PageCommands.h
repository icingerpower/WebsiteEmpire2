#ifndef PAGECOMMANDS_H
#define PAGECOMMANDS_H

#include <QDir>
#include <QStringList>

class AbstractEngine;

/**
 * Console-mode command dispatcher for page operations.
 *
 * Intended to be called from main() before QApplication::exec() when the
 * process is launched with command-line arguments.
 *
 * Usage:
 *   WebsiteEmpire --page generate
 *   WebsiteEmpire --page translate --id <pageId> --lang <langCode>
 *
 * Returns true if a command was handled (prevents the GUI from starting).
 * Returns false if the arguments do not match any known page command.
 *
 * translate subcommand
 * --------------------
 * Loads the page identified by --id, extracts its text blocs, calls the AI
 * translation stub (override translateText() in a subclass or supply a
 * callback), and saves the result as a new page row with the target --lang.
 * The generated page shares the same category data; its permalink gets a
 * "/<lang>/" prefix if it does not already have one.
 *
 * generate subcommand
 * -------------------
 * Calls PageGenerator::generateAll() for the working directory and prints the
 * page count to stdout.
 */
class PageCommands
{
public:
    /**
     * Parses args and dispatches the matching subcommand.
     * workingDir is the directory that contains pages.db / content.db.
     * domain is used when generating (e.g. "example.com").
     * Returns true if a subcommand was handled.
     */
    static bool handle(const QStringList &args,
                       const QDir        &workingDir,
                       const QString     &domain,
                       AbstractEngine    &engine,
                       int                websiteIndex);

private:
    static int runGenerate(const QDir &workingDir, const QString &domain,
                           AbstractEngine &engine, int websiteIndex);
    static int runTranslate(int pageId, const QString &targetLang,
                            const QDir &workingDir);

    /**
     * Stub for AI-assisted translation.
     * Replace with a real implementation that calls the AI API.
     * Returns the translated text, or an empty string on failure.
     */
    static QString translateText(const QString &sourceText,
                                 const QString &targetLang);
};

#endif // PAGECOMMANDS_H
