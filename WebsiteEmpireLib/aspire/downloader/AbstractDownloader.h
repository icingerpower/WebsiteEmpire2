#ifndef ABSTRACTDOWNLOADER_H
#define ABSTRACTDOWNLOADER_H

#include <functional>

#include <QDir>
#include <QFuture>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QSharedPointer>
#include <QStringList>

class AbstractPageAttributes;

class QSettings;

class AbstractDownloader : public QObject
{
    Q_OBJECT

public:
    // Called after each page is parsed: receives the URL and the extracted attribute map.
    // Returns a QFuture<bool>: true to accept the record, false to reject it.
    // Links from the page are followed regardless of the return value.
    // The crawl chain awaits this future before moving to the next URL, so the callback
    // may perform async work (e.g. image fetches) without blocking the event loop.
    using PageParsedCallback = std::function<QFuture<bool>(const QString &url, const QHash<QString, QString> &attributes)>;

    // workingDir defaults to QDir{} so DECLARE_DOWNLOADER still works for
    // type-registry instances (which don't parse anything).
    explicit AbstractDownloader(const QDir &workingDir = QDir{},
                                PageParsedCallback onPageParsed = {},
                                QObject *parent = nullptr);

    virtual QString getId() const = 0;
    virtual QString getName() const = 0;
    virtual QString getDescription() const = 0;
    virtual AbstractPageAttributes *createPageAttributes() const = 0;

    // Creates and returns a new, fully operational instance of this downloader
    // type backed by workingDir.  The caller takes ownership (no parent set).
    // Registry instances (created by DECLARE_DOWNLOADER) use this to spawn
    // working instances without knowing the concrete type.
    virtual AbstractDownloader *createInstance(const QDir &workingDir) const = 0;

    virtual QStringList getUrlsToParse(const QString &content) const = 0;                             // Next links to follow from one page
    virtual QHash<QString, QString> getAttributeValues(const QString &url, const QString &content) const = 0; // Attribute id → value; url is always stored as ID_URL

    // Seed URLs to pass to parse() when starting a fresh crawl.
    // Default implementation returns an empty list; subclasses should override.
    virtual QStringList getSeedUrls() const;

    // Key in the getAttributeValues() hash that holds an image URL to be
    // fetched and stored in the image attribute.  Returns an empty string
    // for downloaders that do not embed image URLs in the text attributes.
    virtual QString getImageUrlAttributeKey() const;

    // Minimum delay in milliseconds between consecutive page fetches during a
    // crawl.  Default is 0 (no delay).  Override to throttle requests to
    // servers with strict rate limits.
    virtual int requestDelayMs() const;

    // Returns true when the fetched content is a valid, complete response worth
    // processing and marking as visited.  Default returns true for any non-empty
    // content.  Override to detect degraded responses (e.g. rate-limited short
    // pages) so the URL is re-enqueued for retry instead of being permanently
    // marked visited.
    virtual bool isFetchSuccessful(const QString &url, const QString &content) const;

    // Replaces the page-parsed callback used by the next parse() call.
    // Useful for wiring a GUI component to an existing downloader after
    // construction.
    void setPageParsedCallback(PageParsedCallback cb);

    // Starts or resumes the crawl.
    // - seedUrls are added to the pending queue; already-visited ones are skipped.
    // - State (visited + pending) is flushed to <workingDir>/<getId()>.ini after
    //   every page, so the session survives a crash.
    // - Implemented as a QFuture continuation chain: no threads, stays on the
    //   Qt event loop. fetchUrl() is the yield point.
    // - Calls getUrlsToParse() after each page to discover new links to follow.
    //
    // With QCoro this collapses to:
    //   QCoro::Task<void> parse(const QStringList &seedUrls = {});
    //   {
    //       enqueuePending(seedUrls);
    //       while (hasPending()) {
    //           const QString url = takePending();
    //           const QString content = co_await fetchUrl(url); // ← yield point
    //           markVisited(url);
    //           co_await m_onPageParsed(url, getAttributeValues(url, content));
    //           enqueuePending(getUrlsToParse(content));
    //       }
    //   }
    QFuture<void> parse(const QStringList &seedUrls = {});

    // Returns true if this downloader supports targeted download from an
    // external URL list (i.e. parseSpecificUrls() is meaningful for it).
    // Default is false; subclasses that accept a URL list should override.
    virtual bool supportsFileUrlDownload() const;

    // Like parse(), but processes only the given URLs without following any
    // links discovered via getUrlsToParse().  Useful for targeted downloads
    // driven by an external URL list (e.g. a Google Analytics export).
    // State is persisted to the same .ini file so the session survives restarts:
    // call parseSpecificUrls() with the same list to resume from where it stopped.
    QFuture<void> parseSpecificUrls(const QStringList &urls);

    // Re-fetches a single URL unconditionally (bypassing the visited set and
    // the pending queue), calls getAttributeValues() on the result, then
    // invokes callback with the extracted attributes.  The visited set and
    // pending queue are not modified.  Returns a future that resolves when
    // the fetch and callback have both completed.
    QFuture<void> reparseUrl(const QString &url, PageParsedCallback callback);

    // Requests the crawl to stop after the current page finishes.
    // processNext() checks this flag before fetching the next URL, so any
    // in-flight DB write or image fetch is allowed to complete first.
    // Designed for graceful Ctrl+C handling in headless mode.
    void requestStop();

    // Emitted when parse() drains the pending queue and there is nothing left
    // to fetch.  Not emitted when a crawl is stopped via requestStop().
    Q_SIGNAL void finished();

    // Emitted when requestStop() causes the crawl to halt before the queue is
    // drained (i.e. after the current in-flight page completes).
    // Not emitted on natural completion — use finished() for that.
    Q_SIGNAL void stopped();

    static const QMap<QString, const AbstractDownloader *> &ALL_DOWNLOADERS();

    class Recorder {
    public:
        Recorder(AbstractDownloader *downloader);
    };

protected:
    // Fetch content of url without blocking the event loop (no thread).
    // Implement with QNetworkAccessManager wrapped in a QPromise<QString>, e.g.:
    //   auto *reply = m_nam.get(QNetworkRequest{QUrl{url}});
    //   auto promise = QSharedPointer<QPromise<QString>>::create();
    //   promise->start();
    //   connect(reply, &QNetworkReply::finished, this, [reply, promise] {
    //       promise->addResult(reply->readAll());
    //       promise->finish();
    //       reply->deleteLater();
    //   });
    //   return promise->future();
    //
    // With QCoro + QNetworkAccessManager it simplifies to:
    //   QCoro::Task<QString> fetchUrl(const QString &url);
    //   { co_return co_await m_nam.get(QNetworkRequest{QUrl{url}})->readAll(); }
    virtual QFuture<QString> fetchUrl(const QString &url) = 0;

    const QDir &workingDir() const;

    static QMap<QString, const AbstractDownloader *> &getDownloaders();

private:
    // One step of the continuation chain: processes the next pending URL,
    // then re-schedules itself via QFuture::then until the queue is empty.
    void processNext(QSharedPointer<QPromise<void>> promise);

    // --- Persistence helpers (backed by <getId()>.ini, lazy-init) ---
    bool isVisited(const QString &url) const;
    void markVisited(const QString &url);          // writes to .ini immediately
    void enqueuePending(const QStringList &urls);  // silently skips already-visited
    QString takePending();
    bool hasPending() const;

    QSettings &settings();   // lazy: created on first call (getId() is available then)
    void loadState();        // call once before first parse; reads .ini into memory
    void savePending();      // rewrites [Pending] array in .ini

    static QString urlKey(const QString &url); // SHA-256 hex — safe QSettings key

    QDir m_workingDir;
    PageParsedCallback m_onPageParsed;
    QSharedPointer<QSettings> m_settings; // null until first settings() call
    QSet<QString> m_visited;
    QSet<QString> m_pendingSet;  // O(1) duplicate guard
    QQueue<QString> m_pending;   // FIFO ordering
    bool m_stateLoaded = false;
    bool m_stopRequested = false;
    // When false, getUrlsToParse() is not called — set by parseSpecificUrls().
    bool m_followLinks = true;
};

#define DECLARE_DOWNLOADER(NEW_CLASS) \
NEW_CLASS instance##NEW_CLASS; \
    AbstractDownloader::Recorder recorder##NEW_CLASS{&instance##NEW_CLASS};

#endif // ABSTRACTDOWNLOADER_H
