#include "AbstractDownloader.h"


AbstractDownloader::AbstractDownloader(QObject *parent)
    : QObject(parent)
{
}

AbstractDownloader::Recorder::Recorder(AbstractDownloader *downloader)
{
    getDownloaders()[downloader->getId()] = downloader;
}

const QMap<QString, const AbstractDownloader *> &AbstractDownloader::ALL_DOWNLOADERS()
{
    return getDownloaders();
}

QMap<QString, const AbstractDownloader *> &AbstractDownloader::getDownloaders()
{
    static QMap<QString, const AbstractDownloader *> downloaders;
    return downloaders;
}
