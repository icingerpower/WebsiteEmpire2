#ifndef LANCHERGENERATOR_H
#define LANCHERGENERATOR_H

#include "AbstractLauncher.h"

// Headless launcher for Claude-assisted data generation.
//
// Triggered by --generator <generator-id>.  Requires --workingDir.
// Two sub-modes are selected by additional options:
//
//   --getjob [n]       Print a JSON array of n pending jobs to stdout (n defaults to 1).
//   --recordjob <json> Record Claude's filled reply; print SUCCESS or FAILURE <reason>.
class LancherGenerator : public AbstractLauncher
{
public:
    static const QString OPTION_NAME;

    QString getOptionName() const override;
    void registerOptions(QCommandLineParser &parser) override;
    void run(const QString &value) override;
};

#endif // LANCHERGENERATOR_H
