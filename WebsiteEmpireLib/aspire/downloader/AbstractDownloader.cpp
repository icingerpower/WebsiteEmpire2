#include "AbstractDownloader.h"

#include <utility>

#include <QCryptographicHash>
#include <QPromise>
#include <QSettings>


AbstractDownloader::AbstractDownloader(const QDir &workingDir, PageParsedCallback onPageParsed, QObject *parent)
    : QObject(parent)
    , m_workingDir(workingDir)
    , m_onPageParsed(std::move(onPageParsed))
{
}

const QDir &AbstractDownloader::workingDir() const
{
    return m_workingDir;
}

QStringList AbstractDownloader::getSeedUrls() const
{
    return {};
}

QString AbstractDownloader::getImageUrlAttributeKey() const
{
    return {};
}

void AbstractDownloader::setPageParsedCallback(PageParsedCallback cb)
{
    m_onPageParsed = std::move(cb);
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

// --- Reparse single URL ---

QFuture<void> AbstractDownloader::reparseUrl(const QString &url, PageParsedCallback callback)
{
    auto promise = QSharedPointer<QPromise<void>>::create();
    promise->start();

    qDebug() << getId() << ": reparsing" << url;

    fetchUrl(url).then(this, [this, url, callback, promise](const QString &content) {
        const QHash<QString, QString> attrs = getAttributeValues(url, content);
        qDebug() << getId() << ": reparse parsed" << url
                 << "— attrs keys:" << attrs.keys();
        if (callback) {
            callback(url, attrs).then(this, [promise](bool) {
                promise->finish();
            }).onFailed(this, [promise](const QException &) {
                promise->finish();
            });
        } else {
            promise->finish();
        }
    }).onFailed(this, [url, promise](const QException &e) {
        qDebug() << "AbstractDownloader: reparseUrl failed for" << url << ":" << e.what();
        promise->finish();
    });

    return promise->future();
}

// --- Parse loop ---

QFuture<void> AbstractDownloader::parse(const QStringList &seedUrls)
{
    m_stopRequested = false;

    if (!m_stateLoaded) {
        m_stateLoaded = true;
        loadState();
        qDebug() << getId() << ": state loaded —"
                 << m_visited.size() << "visited,"
                 << m_pending.size() << "pending";
    }

    const int beforeSize = m_pending.size();
    enqueuePending(seedUrls);
    const int added = m_pending.size() - beforeSize;
    qDebug() << getId() << ": enqueuePending" << seedUrls
             << "→ added" << added
             << "(seeds already visited:" << (seedUrls.size() - added) << ")";

    qDebug() << getId() << ": starting parse with" << m_pending.size() << "pending URLs";

    auto promise = QSharedPointer<QPromise<void>>::create();
    promise->start();
    processNext(promise);
    return promise->future();
}

void AbstractDownloader::requestStop()
{
    qDebug() << getId() << ": stop requested — will finish after current page";
    m_stopRequested = true;
}

void AbstractDownloader::processNext(QSharedPointer<QPromise<void>> promise)
{
    if (m_stopRequested) {
        qDebug() << getId() << ": stop requested — aborting crawl (state saved)";
        promise->finish();
        emit stopped();
        return;
    }

    if (!hasPending()) {
        qDebug() << getId() << ": no more pending URLs — finished"
                 << "(total visited:" << m_visited.size() << ")";
        emit finished();
        promise->finish();
        return;
    }

    const QString url = takePending();
    qDebug() << getId() << ": fetching" << url
             << "(" << m_pending.size() << "remaining)";

    fetchUrl(url).then(this, [this, url, promise](const QString &content) {
        markVisited(url);
        const QHash<QString, QString> attrs = getAttributeValues(url, content);
        const QStringList newUrls = getUrlsToParse(content);
        qDebug() << getId() << ": parsed" << url
                 << "— attrs keys:" << attrs.keys()
                 << "— new URLs:" << newUrls.size();
        if (m_onPageParsed) {
            // Await the callback's future so async work (e.g. image fetches) completes
            // before advancing to the next URL.  The event loop remains free throughout.
            m_onPageParsed(url, attrs).then(this, [this, newUrls, promise](bool /*accepted*/) {
                enqueuePending(newUrls);
                processNext(promise);
            }).onFailed(this, [this, newUrls, promise](const QException &) {
                enqueuePending(newUrls);
                processNext(promise);
            });
        } else {
            enqueuePending(newUrls);
            processNext(promise);
        }
    }).onFailed(this, [this, url, promise](const QException &e) {
        qDebug() << getId() << ": failed to fetch" << url << ":" << e.what();
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
