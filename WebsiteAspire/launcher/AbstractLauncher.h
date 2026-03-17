#ifndef ABSTRACTLAUNCHER_H
#define ABSTRACTLAUNCHER_H

#include <QMap>
#include <QString>

// Base class for headless launch modes triggered by CLI arguments.
//
// Subclasses register themselves via DECLARE_LAUNCHER() and are invoked
// by main() when the matching option is found in the argument list.
// All registered launchers are accessible through ALL_LAUNCHERS().
//
// CLI convention: options are passed as --<name> <value>.
// getOptionName() returns only the name part (without "--"); main() prepends "--".
// OPTION_WORKING_DIR is the name for the working-directory option, also without "--".
class AbstractLauncher
{
public:
    virtual ~AbstractLauncher() = default;

    // Option name (without "--") for the working-directory argument.
    // Defined here so both main() and copyCommand() reference one constant.
    static const QString OPTION_WORKING_DIR;

    // The CLI option name (without "--") that triggers this launcher, e.g. "download".
    virtual QString getOptionName() const = 0;

    // Execute the launcher with the value passed after the option name.
    virtual void run(const QString &value) = 0;

    // Returns all registered launchers keyed by option name.
    static const QMap<QString, AbstractLauncher *> &ALL_LAUNCHERS();

    // Used by DECLARE_LAUNCHER to register a launcher instance at startup.
    class Recorder
    {
    public:
        explicit Recorder(AbstractLauncher *launcher);
    };

private:
    static QMap<QString, AbstractLauncher *> &getLaunchers();
};

#define DECLARE_LAUNCHER(NEW_CLASS)                                    \
    NEW_CLASS instance##NEW_CLASS;                                     \
    AbstractLauncher::Recorder recorder##NEW_CLASS{&instance##NEW_CLASS};

#endif // ABSTRACTLAUNCHER_H
