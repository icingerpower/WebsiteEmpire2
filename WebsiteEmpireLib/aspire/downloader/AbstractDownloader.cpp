#include "AbstractDownloader.h"

#include <utility>

#include <QCryptographicHash>
#include <QFile>
#include <QPromise>
#include <QSettings>
#include <QTimer>
#include <QUrl>


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

int AbstractDownloader::requestDelayMs() const
{
    return 0;
}

bool AbstractDownloader::isFetchSuccessful(const QString &/*url*/, const QString &content) const
{
    return !content.isEmpty();
}

bool AbstractDownloader::supportsFileUrlDownload() const
{
    return false;
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
    m_followLinks = true;
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

QFuture<void> AbstractDownloader::parseSpecificUrls(const QStringList &urls)
{
    m_followLinks = false;
    m_stopRequested = false;

    if (!m_stateLoaded) {
        m_stateLoaded = true;
        loadState();
        qDebug() << getId() << ": state loaded —"
                 << m_visited.size() << "visited,"
                 << m_pending.size() << "pending";
    }

    int alreadyVisited = 0;
    int alreadyPending = 0;
    for (const QString &url : urls) {
        if (m_visited.contains(url))       { ++alreadyVisited; }
        else if (m_pendingSet.contains(url)) { ++alreadyPending; }
    }
    const int beforeSize = m_pending.size();
    enqueuePending(urls);
    const int added = m_pending.size() - beforeSize;
    qDebug() << getId() << ": parseSpecificUrls: enqueuePending" << urls.size() << "URLs"
             << "→ added" << added
             << "(already visited:" << alreadyVisited
             << ", already pending:" << alreadyPending << ")";

    qDebug() << getId() << ": starting specific-URLs parse with" << m_pending.size() << "pending URLs";

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

    auto doFetch = [this, url, promise]() {
        fetchUrl(url).then(this, [this, url, promise](const QString &content) {
            if (!isFetchSuccessful(url, content)) {
                // Server returned a degraded response (e.g. rate-limited short page).
                // Re-enqueue at end of queue so it is retried later; do NOT mark visited.
                qDebug() << getId() << ": fetch degraded for" << url
                         << "(content length:" << content.length() << ") — re-enqueued for retry";

                // Save the degraded content to disk for inspection.
                if (m_workingDir.exists()) {
                    QDir debugDir(m_workingDir.filePath(QStringLiteral("debug_pages")));
                    debugDir.mkpath(QStringLiteral("."));
                    const QString urlPath = QUrl(url).path();
                    QString baseName = urlPath.section(QLatin1Char('/'), -1);
                    if (baseName.isEmpty()) {
                        baseName = QStringLiteral("root");
                    }
                    // Truncate to 80 chars to stay within filename limits.
                    if (baseName.length() > 80) {
                        baseName = baseName.left(80);
                    }
                    const QString filePath = debugDir.filePath(baseName + QStringLiteral(".txt"));
                    QFile f(filePath);
                    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        f.write(content.toUtf8());
                        f.close();
                        qDebug() << getId() << ": degraded content saved to" << filePath;
                    }
                }

                enqueuePending({url});
                processNext(promise);
                return;
            }
            markVisited(url);
            const QHash<QString, QString> attrs = getAttributeValues(url, content);
            const QStringList newUrls = m_followLinks ? getUrlsToParse(content) : QStringList{};
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
    };

    const int delay = requestDelayMs();
    if (delay > 0) {
        QTimer::singleShot(delay, this, std::move(doFetch));
    } else {
        doFetch();
    }
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
