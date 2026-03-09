#ifndef ABSTRACTDOWNLOADER_H
#define ABSTRACTDOWNLOADER_H

#include <QDir>
#include <QFuture>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QSharedPointer>
#include <QStringList>

class QSettings;

class AbstractDownloader : public QObject
{
    Q_OBJECT

public:
    // workingDir defaults to QDir{} so DECLARE_DOWNLOADER still works for
    // type-registry instances (which don't parse anything).
    explicit AbstractDownloader(const QDir &workingDir = QDir{}, QObject *parent = nullptr);

    virtual QString getId() const = 0;
    virtual QString getName() const = 0;
    virtual QString getDescription() const = 0;

    virtual QStringList getUrlsToParse(const QString &content) const = 0;          // Next links to follow from one page
    virtual QHash<QString, QString> getAttributeValues(const QString &content) const = 0; // Attribute id → value

    // Starts or resumes the crawl.
    // - seedUrls are added to the pending queue; already-visited ones are skipped.
    // - State (visited + pending) is flushed to <workingDir>/<getId()>.ini after
    //   every page, so the session survives a crash.
    // - Implemented as a QFuture continuation chain: no threads, stays on the
    //   Qt event loop. fetchUrl() is the yield point.
    //
    // With QCoro this collapses to:
    //   QCoro::Task<void> parse(const QStringList &seedUrls = {});
    //   {
    //       enqueuePending(seedUrls);
    //       while (hasPending()) {
    //           const QString url = takePending();
    //           const QString content = co_await fetchUrl(url); // ← yield point
    //           markVisited(url);
    //           emit pageParsed(url, getAttributeValues(content));
    //           enqueuePending(getUrlsToParse(content));
    //       }
    //   }
    QFuture<void> parse(const QStringList &seedUrls = {});

    static const QMap<QString, const AbstractDownloader *> &ALL_DOWNLOADERS();

    class Recorder {
    public:
        Recorder(AbstractDownloader *downloader);
    };

signals:
    void pageParsed(const QString &url, const QHash<QString, QString> &attributes);

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
    QSharedPointer<QSettings> m_settings; // null until first settings() call
    QSet<QString> m_visited;
    QSet<QString> m_pendingSet;  // O(1) duplicate guard
    QQueue<QString> m_pending;   // FIFO ordering
    bool m_stateLoaded = false;
};

#define DECLARE_DOWNLOADER(NEW_CLASS) \
NEW_CLASS instance##NEW_CLASS; \
    AbstractDownloader::Recorder recorder##NEW_CLASS{&instance##NEW_CLASS};

#endif // ABSTRACTDOWNLOADER_H
