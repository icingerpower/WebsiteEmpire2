#include "PaneAspire.h"
#include "ui_PaneAspire.h"

#include <QDir>
#include <QListWidgetItem>
#include <QStandardPaths>

#include "WidgetDownloader.h"
#include "aspire/attributes/AbstractPageAttributes.h"
#include "aspire/downloader/AbstractDownloader.h"
#include "aspire/downloader/DownloadedPagesTable.h"

PaneAspire::PaneAspire(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneAspire)
    , m_firstTimeVisible(false)
{
    ui->setupUi(this);
    _init();
}

PaneAspire::~PaneAspire()
{
    delete ui;
}

void PaneAspire::setVisible(bool visible)
{
    QWidget::setVisible(visible);
}

void PaneAspire::_init()
{
    // Working directory: one shared folder for all downloaders' .db and .ini files.
    const QString dataPath =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir workDir(dataPath + QStringLiteral("/aspire"));
    workDir.mkpath(QStringLiteral("."));

    // Each entry in ALL_DOWNLOADERS() is a static registry instance created by
    // DECLARE_DOWNLOADER.  We ask it to spawn a proper working instance (with
    // the correct workingDir) via createInstance().
    for (const AbstractDownloader *registryDl : AbstractDownloader::ALL_DOWNLOADERS()) {
        // --- Downloader (parented first so it outlives the table) ---
        AbstractDownloader *dl = registryDl->createInstance(workDir);
        dl->setParent(this);

        // --- Page-attributes model for viewAttributes() dialog ---
        // createPageAttributes() returns an uninitialised instance; call init()
        // so that its QAbstractTableModel rows are populated.
        AbstractPageAttributes *attrs = dl->createPageAttributes();
        attrs->setParent(this);
        attrs->init();

        // --- Table model (parented after the downloader so it is destroyed first) ---
        auto *table = new DownloadedPagesTable(workDir, dl, this);

        // --- Widget (owned by stackedWidget, destroyed before this's children) ---
        auto *widget = new WidgetDownloader(ui->stackedWidget);
        widget->init(attrs, table);
        ui->stackedWidget->addWidget(widget);

        // --- Sidebar entry ---
        new QListWidgetItem(registryDl->getName(), ui->listWidgetDownloaders);
    }

    // Select the first downloader if any are registered.
    if (ui->listWidgetDownloaders->count() > 0) {
        ui->listWidgetDownloaders->setCurrentRow(0);
    }
}
