#include "LancherGenerator.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QException>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "aspire/generator/AbstractGenerator.h"
#include "workingdirectory/WorkingDirectoryManager.h"

const QString LancherGenerator::OPTION_NAME = QStringLiteral("generator");

DECLARE_LAUNCHER(LancherGenerator)

QString LancherGenerator::getOptionName() const
{
    return OPTION_NAME;
}

void LancherGenerator::registerOptions(QCommandLineParser &parser)
{
    parser.addOption(QCommandLineOption(
        QStringLiteral("getjob"),
        QCoreApplication::tr("Print the next N pending jobs to stdout (N defaults to 1)."),
        QStringLiteral("n")));
    parser.addOption(QCommandLineOption(
        QStringLiteral("recordjob"),
        QCoreApplication::tr("Record Claude's filled JSON reply."),
        QStringLiteral("json")));
}

void LancherGenerator::run(const QString &value)
{
    QTextStream out(stdout);

    const auto &allGen = AbstractGenerator::ALL_GENERATORS();
    if (!allGen.contains(value)) {
        out << QStringLiteral("FAILURE\nUnknown generator id: ") << value << QStringLiteral("\n");
        out.flush();
        QCoreApplication::quit();
        return;
    }

    QDir workDir(WorkingDirectoryManager::instance()->workingDir().path()
                 + QStringLiteral("/generator"));
    workDir.mkpath(QStringLiteral("."));

    AbstractGenerator *gen = allGen.value(value)->createInstance(workDir);

    // Scan raw arguments for --getjob [n] and --recordjob <json>.
    // Manual scanning keeps --getjob count truly optional (Qt's parser requires
    // a value when a value name is declared).
    const QStringList args = QCoreApplication::arguments();
    bool doGetJob = false;
    bool doRecordJob = false;
    int getJobCount = 1;
    QString recordJobJson;

    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("--getjob")) {
            doGetJob = true;
            if (i + 1 < args.size() && !args.at(i + 1).startsWith(QLatin1Char('-'))) {
                bool ok = false;
                const int n = args.at(i + 1).toInt(&ok);
                if (ok && n > 0) {
                    getJobCount = n;
                    ++i;
                }
            }
        } else if (args.at(i) == QStringLiteral("--recordjob") && i + 1 < args.size()) {
            doRecordJob = true;
            recordJobJson = args.at(++i);
        }
    }

    if (doGetJob) {
        QJsonArray jobs;
        for (int i = 0; i < getJobCount; ++i) {
            const QString jobStr = gen->getNextJob();
            if (jobStr.isEmpty()) {
                break;
            }
            jobs << QJsonDocument::fromJson(jobStr.toUtf8()).object();
        }
        out << QString::fromUtf8(QJsonDocument(jobs).toJson(QJsonDocument::Indented));
        out.flush();
    } else if (doRecordJob) {
        try {
            const bool ok = gen->recordReply(recordJobJson);
            if (ok) {
                out << QStringLiteral("SUCCESS\n");
            } else {
                out << QStringLiteral("FAILURE\nInvalid reply or unknown jobId.\n");
            }
        } catch (const QException &ex) {
            out << QStringLiteral("FAILURE\n") << QString::fromUtf8(ex.what()) << QStringLiteral("\n");
        }
        out.flush();
    } else {
        out << QStringLiteral("FAILURE\nMust specify --getjob or --recordjob.\n");
        out.flush();
    }

    delete gen;
    QCoreApplication::quit();
}
