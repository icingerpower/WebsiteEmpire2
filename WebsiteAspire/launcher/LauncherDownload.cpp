#include "LauncherDownload.h"

#include <csignal>
#include <unistd.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>
#include <QSocketNotifier>
#include <QTextStream>

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
        [table, imageUrlKey, nam](const QString &url,
                                  const QHash<QString, QString> &attrs) -> QFuture<bool> {
            auto promise = QSharedPointer<QPromise<bool>>::create();
            promise->start();

            if (attrs.isEmpty()) {
                qDebug() << "LauncherDownload: not recorded (no attributes parsed)" << url;
                promise->addResult(false);
                promise->finish();
                return promise->future();
            }

            auto record = [table, attrs, imageUrlKey, promise, url](QList<QSharedPointer<QImage>> images) {
                QHash<QString, QString> textAttrs = attrs;
                textAttrs.remove(imageUrlKey);

                const QHash<QString, QList<QSharedPointer<QImage>>> imageAttrs{
                    {PageAttributesProduct::ID_IMAGES, images}
                };

                try {
                    table->recordPage(textAttrs, imageAttrs);
                    qDebug() << "LauncherDownload: URL added, NEW TOTAL IS:"
                             << table->rowCount() << url;
                } catch (const QException &ex) {
                    qDebug() << "LauncherDownload: not recorded (validation failed)" << url
                             << "—" << ex.what();
                }

                promise->addResult(true);
                promise->finish();
            };

            // --- Fetch product images asynchronously (no nested event loop) ---
            // imageUrlKey may hold a semicolon-separated list of URLs; download all
            // in parallel and call record() once every reply has been received.
            const QStringList imageUrls = attrs.value(imageUrlKey)
                                              .split(QLatin1Char(';'), Qt::SkipEmptyParts);
            if (!imageUrls.isEmpty()) {
                auto collected = QSharedPointer<QList<QSharedPointer<QImage>>>::create();
                auto remaining = QSharedPointer<int>::create(imageUrls.size());

                for (const QString &imgUrl : imageUrls) {
                    QNetworkRequest req{QUrl{imgUrl}};
                    req.setHeader(QNetworkRequest::UserAgentHeader,
                                  QStringLiteral("Mozilla/5.0 (compatible; WebsiteEmpire/1.0)"));
                    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                     QNetworkRequest::NoLessSafeRedirectPolicy);

                    QNetworkReply *reply = nam->get(req);
                    QObject::connect(reply, &QNetworkReply::finished, reply,
                                     [reply, collected, remaining, record]() mutable {
                                         auto img = QSharedPointer<QImage>::create();
                                         if (img->loadFromData(reply->readAll())) {
                                             *collected << img;
                                         }
                                         reply->deleteLater();
                                         if (--(*remaining) == 0) {
                                             if (collected->isEmpty()) {
                                                 auto placeholder = QSharedPointer<QImage>::create(
                                                     200, 200, QImage::Format_RGB32);
                                                 placeholder->fill(Qt::white);
                                                 *collected << placeholder;
                                             }
                                             record(std::move(*collected));
                                         }
                                     });
                }
            } else {
                auto placeholder = QSharedPointer<QImage>::create(200, 200, QImage::Format_RGB32);
                placeholder->fill(Qt::white);
                record({placeholder});
            }

            return promise->future();
        });

    auto quit = [holder]() {
        holder->deleteLater();
        QCoreApplication::quit();
    };
    QObject::connect(dl, &AbstractDownloader::finished, holder, [quit]() {
        qDebug() << "LauncherDownload: download finished.";
        quit();
    });
    QObject::connect(dl, &AbstractDownloader::stopped, holder, [quit]() {
        qDebug() << "LauncherDownload: download stopped.";
        quit();
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

    // --- Resolve --urls file path if provided ---
    QCommandLineParser argsParser;
    argsParser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    const QCommandLineOption urlsOpt(QStringLiteral("urls"), QString(), QStringLiteral("filepath"));
    argsParser.addOption(urlsOpt);
    argsParser.parse(QCoreApplication::arguments()); // parse(), not process(): ignore unknown opts

    const QString urlsFilePath = argsParser.value(urlsOpt);

    if (!urlsFilePath.isEmpty() && dl->supportsFileUrlDownload()) {
        QFile file(urlsFilePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "LauncherDownload: cannot open URLs file:" << urlsFilePath;
            QCoreApplication::quit();
            return;
        }

        QStringList urls;
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (!line.isEmpty()) {
                urls << line;
            }
        }

        qDebug() << "LauncherDownload: launching from file URLs, count:" << urls.size();
        dl->parseSpecificUrls(urls);
    } else {
        dl->parse(dl->getSeedUrls());
    }
}
