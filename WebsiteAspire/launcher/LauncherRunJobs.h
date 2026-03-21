#ifndef LAUNCHERRUNJOBS_H
#define LAUNCHERRUNJOBS_H

#include "AbstractLauncher.h"

class QCommandLineParser;

// Headless launcher that runs generator jobs automatically through the Claude CLI.
//
// Triggered by --runjobs <generator-id>.  Requires --workingdir.
// Optional sub-option (scanned from raw arguments, not via the Qt parser):
//   --sessions N   Number of parallel Claude sessions to run (1-10, default 1).
//
// The loop runs until all jobs are complete, or until SIGINT / SIGTERM is
// received.  On signal the current session(s) are allowed to finish their
// current job before the process exits cleanly.
//
// Press Ctrl+C once to request a graceful stop; the running Claude process
// will finish its current job before the application quits.
class LauncherRunJobs : public AbstractLauncher
{
public:
    static const QString OPTION_NAME;
    static const QString OPTION_SESSIONS;

    QString getOptionName() const override;
    void registerOptions(QCommandLineParser &parser) override;
    void run(const QString &value) override;
};

#endif // LAUNCHERRUNJOBS_H
