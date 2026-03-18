#include "LauncherDownload.h"

#include <csignal>
#include <unistd.h>

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>
#include <QSocketNotifier>

#include "aspire/attributes/AbstractPageAttributes.h"
#include "aspire/attributes/PageAttributesProduct.h"
#include "aspire/downloader/AbstractDownloader.h"
#include "aspire/downloader/DownloadedPagesTable.h"
#include "workingdirectory/WorkingDirectoryManager.h"

DECLARE_LAUNCHER(LauncherDownload)

const QString LauncherDownload::OPTION_NAME = QStringLiteral("download");

// Self-pipe used to relay SIGINT from the async-signal context into Qt's event loop.
static int s_sigIntPipe[2] = {-1, -1};

static void sigIntHandler(int)
{
    const char byte = 1;
    // write() is async-signal-safe; errors are intentionally ignored here.
    [[maybe_unused]] ssize_t r = ::write(s_sigIntPipe[1], &byte, sizeof(byte));
}

QString LauncherDownload::getOptionName() const
{
    return OPTION_NAME;
}

void LauncherDownload::run(const QString &value)
{
    const auto &allDl = AbstractDownloader::ALL_DOWNLOADERS();
    if (!allDl.contains(value)) {
        qDebug() << "LauncherDownload: unknown downloader id:" << value;
        QCoreApplication::quit();
        return;
    }

    QDir workDir(WorkingDirectoryManager::instance()->workingDir().path()
                 + QStringLiteral("/aspire"));
    workDir.mkpath(QStringLiteral("."));

    // holder owns all dynamically-allocated objects; deleted when the download finishes.
    auto *holder = new QObject(nullptr);

    AbstractDownloader *dl = allDl.value(value)->createInstance(workDir);
    dl->setParent(holder);

    AbstractPageAttributes *attrs = dl->createPageAttributes();
    attrs->setParent(holder);
    attrs->init();

    auto *table = new DownloadedPagesTable(workDir, dl, holder);
    auto *nam = new QNetworkAccessManager(holder);

    const QString imageUrlKey = dl->getImageUrlAttributeKey();

    dl->setPageParsedCallback(
        [table, imageUrlKey, nam](const QString & /*url*/,
                                  const QHash<QString, QString> &attrs) -> QFuture<bool> {
            auto promise = QSharedPointer<QPromise<bool>>::create();
            promise->start();

            if (attrs.isEmpty()) {
                promise->addResult(false);
                promise->finish();
                return promise->future();
            }

            auto record = [table, attrs, imageUrlKey, promise](QList<QSharedPointer<QImage>> images) {
                QHash<QString, QString> textAttrs = attrs;
                textAttrs.remove(imageUrlKey);

                const QHash<QString, QList<QSharedPointer<QImage>>> imageAttrs{
                    {PageAttributesProduct::ID_IMAGES, images}
                };

                try {
                    table->recordPage(textAttrs, imageAttrs);
                    qDebug() << "LauncherDownload: recorded page, total:"
                             << table->rowCount();
                } catch (const QException &ex) {
                    qDebug() << "LauncherDownload: record failed:" << ex.what();
                }

                promise->addResult(true);
                promise->finish();
            };

            // --- Fetch product image asynchronously (no nested event loop) ---
            const QString imageUrl = attrs.value(imageUrlKey);
            if (!imageUrl.isEmpty()) {
                QNetworkRequest req{QUrl{imageUrl}};
                req.setHeader(QNetworkRequest::UserAgentHeader,
                              QStringLiteral("Mozilla/5.0 (compatible; WebsiteEmpire/1.0)"));
                req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::NoLessSafeRedirectPolicy);

                QNetworkReply *reply = nam->get(req);
                QObject::connect(reply, &QNetworkReply::finished, reply,
                                 [reply, record]() mutable {
                                     QList<QSharedPointer<QImage>> images;
                                     const QByteArray imgData = reply->readAll();
                                     reply->deleteLater();

                                     auto img = QSharedPointer<QImage>::create();
                                     if (img->loadFromData(imgData)) {
                                         images << img;
                                     }
                                     if (images.isEmpty()) {
                                         auto placeholder = QSharedPointer<QImage>::create(
                                             200, 200, QImage::Format_RGB32);
                                         placeholder->fill(Qt::white);
                                         images << placeholder;
                                     }
                                     record(std::move(images));
                                 });
            } else {
                auto placeholder = QSharedPointer<QImage>::create(
                    200, 200, QImage::Format_RGB32);
                placeholder->fill(Qt::white);
                record({placeholder});
            }

            return promise->future();
        });

    QObject::connect(dl, &AbstractDownloader::finished, holder, [holder]() {
        qDebug() << "LauncherDownload: download finished.";
        holder->deleteLater();
        QCoreApplication::quit();
    });

    // --- Graceful Ctrl+C: let the current page's DB/INI write finish first ---
    if (::pipe(s_sigIntPipe) == 0) {
        ::signal(SIGINT, sigIntHandler);
        auto *notifier = new QSocketNotifier(s_sigIntPipe[0], QSocketNotifier::Read, holder);
        QObject::connect(notifier, &QSocketNotifier::activated, dl, [dl, notifier]() {
            notifier->setEnabled(false);
            char byte;
            [[maybe_unused]] ssize_t r = ::read(s_sigIntPipe[0], &byte, sizeof(byte));
            qDebug() << "LauncherDownload: Ctrl+C received — finishing current page then stopping...";
            dl->requestStop();
        });
    }

    dl->parse(dl->getSeedUrls());
}
