#ifndef ABSTRACTLAUNCHER_EMPIRE_H
#define ABSTRACTLAUNCHER_EMPIRE_H

#include <QMap>
#include <QString>

// Base class for headless launch modes triggered by CLI arguments.
//
// Subclasses register themselves via DECLARE_LAUNCHER() and are invoked
// by main() when the matching option is present in the argument list.
// All registered launchers are accessible through ALL_LAUNCHERS().
//
// CLI convention:
//   Value launcher  : --<name> <value>   (isFlag() == false, default)
//   Flag launcher   : --<name>           (isFlag() == true, no value)
//
// OPTION_WORKING_DIR is the name for the working-directory option (without "--").
// main() sets up the working directory before calling run().
class AbstractLauncher
{
public:
    virtual ~AbstractLauncher() = default;

    // Option name (without "--") for the working-directory argument.
    static const QString OPTION_WORKING_DIR;

    // The CLI option name (without "--") that triggers this launcher, e.g. "translate".
    virtual QString getOptionName() const = 0;

    // Returns true if this launcher is a flag option (no value required).
    // Flag launchers receive an empty string in run().
    // Default: false.
    virtual bool isFlag() const;

    // Execute the launcher.  value is the string after the option name (empty
    // for flag launchers).  Called after WorkingDirectoryManager is initialised.
    virtual void run(const QString &value) = 0;

    // Returns all registered launchers keyed by option name.
    static const QMap<QString, AbstractLauncher *> &ALL_LAUNCHERS();

    // Used by DECLARE_LAUNCHER to register an instance at startup.
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

#endif // ABSTRACTLAUNCHER_EMPIRE_H
