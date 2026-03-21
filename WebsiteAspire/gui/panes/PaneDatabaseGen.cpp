#include "PaneDatabaseGen.h"
#include "ui_PaneDatabaseGen.h"

#include <QDir>
#include <QListWidgetItem>

#include "WidgetGenerator.h"
#include "aspire/generator/AbstractGenerator.h"
#include "workingdirectory/WorkingDirectoryManager.h"

PaneDatabaseGen::PaneDatabaseGen(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneDatabaseGen)
{
    ui->setupUi(this);
    _init();
}

PaneDatabaseGen::~PaneDatabaseGen()
{
    delete ui;
}

void PaneDatabaseGen::_init()
{
    // Working directory: one shared folder for all generators' .ini and result files.
    QDir workDir(WorkingDirectoryManager::instance()->workingDir().path()
                 + QStringLiteral("/generator"));
    workDir.mkpath(QStringLiteral("."));

    // Each entry in ALL_GENERATORS() is a static registry prototype created by
    // DECLARE_GENERATOR.  We ask it to spawn a proper working instance (with the
    // correct workingDir) via createInstance().
    for (const AbstractGenerator *protoGen : AbstractGenerator::ALL_GENERATORS()) {
        AbstractGenerator *gen = protoGen->createInstance(workDir);
        gen->setParent(this);

        auto *widget = new WidgetGenerator(ui->stackedWidget);
        widget->init(gen);
        ui->stackedWidget->addWidget(widget);

        new QListWidgetItem(protoGen->getName(), ui->listWidgetGenerators);
    }

    if (ui->listWidgetGenerators->count() > 0) {
        ui->listWidgetGenerators->setCurrentRow(0);
    }
}
