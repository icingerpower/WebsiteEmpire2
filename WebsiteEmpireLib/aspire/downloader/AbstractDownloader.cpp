#include "AbstractDownloader.h"

#include <QCryptographicHash>
#include <QPromise>
#include <QSettings>


AbstractDownloader::AbstractDownloader(const QDir &workingDir, QObject *parent)
    : QObject(parent)
    , m_workingDir(workingDir)
{
}

const QDir &AbstractDownloader::workingDir() const
{
    return m_workingDir;
}

// --- Registry ---

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

// --- Parse loop ---

QFuture<void> AbstractDownloader::parse(const QStringList &seedUrls)
{
    if (!m_stateLoaded) {
        m_stateLoaded = true;
        loadState();
    }
    enqueuePending(seedUrls);

    auto promise = QSharedPointer<QPromise<void>>::create();
    promise->start();
    processNext(promise);
    return promise->future();
}

void AbstractDownloader::processNext(QSharedPointer<QPromise<void>> promise)
{
    if (!hasPending()) {
        promise->finish();
        return;
    }

    const QString url = takePending();

    fetchUrl(url).then(this, [this, url, promise](const QString &content) {
        markVisited(url);
        emit pageParsed(url, getAttributeValues(content));
        enqueuePending(getUrlsToParse(content));
        processNext(promise);
    }).onFailed(this, [this, url, promise](const QException &e) {
        qDebug() << "Failed to fetch" << url << ":" << e.what();
        processNext(promise); // skip and continue
    });
}

// --- Persistence ---

QSettings &AbstractDownloader::settings()
{
    if (!m_settings) {
        m_settings = QSharedPointer<QSettings>::create(
            m_workingDir.filePath(getId() + ".ini"),
            QSettings::IniFormat
        );
    }
    return *m_settings;
}

QString AbstractDownloader::urlKey(const QString &url)
{
    return QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex();
}

void AbstractDownloader::loadState()
{
    auto &s = settings();

    s.beginGroup("Visited");
    for (const QString &key : s.childKeys())
        m_visited.insert(s.value(key).toString());
    s.endGroup();

    const int count = s.beginReadArray("Pending");
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        const QString url = s.value("url").toString();
        if (!m_visited.contains(url) && !m_pendingSet.contains(url)) {
            m_pending.enqueue(url);
            m_pendingSet.insert(url);
        }
    }
    s.endArray();
}

bool AbstractDownloader::isVisited(const QString &url) const
{
    return m_visited.contains(url);
}

void AbstractDownloader::markVisited(const QString &url)
{
    m_visited.insert(url);
    m_pendingSet.remove(url);
    settings().setValue("Visited/" + urlKey(url), url);
    settings().sync();
    savePending();
}

void AbstractDownloader::enqueuePending(const QStringList &urls)
{
    bool changed = false;
    for (const QString &url : urls) {
        if (!m_visited.contains(url) && !m_pendingSet.contains(url)) {
            m_pending.enqueue(url);
            m_pendingSet.insert(url);
            changed = true;
        }
    }
    if (changed)
        savePending();
}

QString AbstractDownloader::takePending()
{
    const QString url = m_pending.dequeue();
    m_pendingSet.remove(url);
    savePending();
    return url;
}

bool AbstractDownloader::hasPending() const
{
    return !m_pending.isEmpty();
}

void AbstractDownloader::savePending()
{
    auto &s = settings();
    s.beginWriteArray("Pending");
    int i = 0;
    for (const QString &url : m_pending) {
        s.setArrayIndex(i++);
        s.setValue("url", url);
    }
    s.endArray();
    s.sync();
}
