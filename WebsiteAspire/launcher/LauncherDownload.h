#ifndef LAUNCHERDOWNLOAD_H
#define LAUNCHERDOWNLOAD_H

#include "AbstractLauncher.h"

// Headless launcher that replicates WidgetDownloader::download() from the CLI.
//
// Triggered by --download <downloader-id>.  Requires --workingDir to have been
// set first so that WorkingDirectoryManager::instance()->workingDir() is valid.
class LauncherDownload : public AbstractLauncher
{
public:
    // Option name (without "--") used to trigger this launcher.
    // Exposed as a static constant so callers (e.g. copyCommand) can reference
    // it without needing an instance.
    static const QString OPTION_NAME;

    QString getOptionName() const override;
    void run(const QString &value) override;
};

#endif // LAUNCHERDOWNLOAD_H
