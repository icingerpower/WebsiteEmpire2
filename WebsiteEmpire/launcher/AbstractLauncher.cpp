#include "AbstractLauncher.h"

#include "aicli/AbstractCli.h"
#include <QObject>

const QString AbstractLauncher::OPTION_WORKING_DIR = QStringLiteral("workingDir");
const QString AbstractLauncher::OPTION_CLI         = QStringLiteral("cli");

AbstractCli *AbstractLauncher::resolveCli(const QStringList &args, QString &error)
{
    error.clear();
    const auto &clis = AbstractCli::ALL_CLIS();
    const int index = args.indexOf(QStringLiteral("--") + OPTION_CLI);
    if (index < 0) {
        if (!clis.isEmpty()) {
            return clis.first();
        }
        error = QObject::tr("No AI CLI registered.");
        return nullptr;
    }
    if (index + 1 >= args.size() || args.at(index + 1).isEmpty()
        || args.at(index + 1).startsWith(QStringLiteral("--"))) {
        error = QObject::tr("Missing value for --cli.");
        return nullptr;
    }

    const QString &name = args.at(index + 1);
    QStringList available;
    for (AbstractCli *cli : clis) {
        if (cli->getName().compare(name, Qt::CaseInsensitive) == 0
            || cli->getExecutable().compare(name, Qt::CaseInsensitive) == 0) {
            return cli;
        }
        available.append(cli->getName());
    }
    error = QObject::tr("Unknown AI CLI '%1'. Available CLIs: %2.")
                .arg(name, available.join(QStringLiteral(", ")));
    return nullptr;
}

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
