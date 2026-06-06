#include "AbstractLauncher.h"

const QString AbstractLauncher::OPTION_WORKING_DIR = QStringLiteral("workingDir");
const QString AbstractLauncher::OPTION_CLI         = QStringLiteral("cli");

bool AbstractLauncher::isFlag() const
{
    return false;
}

AbstractLauncher::Recorder::Recorder(AbstractLauncher *launcher)
{
    getLaunchers().insert(launcher->getOptionName(), launcher);
}

const QMap<QString, AbstractLauncher *> &AbstractLauncher::ALL_LAUNCHERS()
{
    return getLaunchers();
}

QMap<QString, AbstractLauncher *> &AbstractLauncher::getLaunchers()
{
    static QMap<QString, AbstractLauncher *> map;
    return map;
}
