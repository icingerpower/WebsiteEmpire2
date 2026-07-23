#include "OrderedImageDownloader.h"

#include <utility>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "OrderedImageCollector.h"
#include "aspire/attributes/PageAttributesProduct.h"

namespace OrderedImageDownloader {

void fetchInOrder(QNetworkAccessManager *nam,
                  const QStringList &imageUrls,
                  const QString &productUrl,
                  std::function<void(QList<QSharedPointer<QImage>>)> onComplete)
{
    // The collector holds the position-as-identity buffer and fires onComplete
    // once every reply has been accounted for. It is kept alive by the reply
    // lambdas (captured by value) until the last reply finishes. It also enforces
    // the shared minimum-image-size policy so every crawl/record/reparse path
    // filters small images identically (§XV-4: hoist duplicated logic).
    auto collector = QSharedPointer<OrderedImageCollector>::create(
        imageUrls.size(), std::move(onComplete),
        PageAttributesProduct::MIN_IMAGE_SIDE_PX, imageUrls, productUrl);

    for (int i = 0; i < imageUrls.size(); ++i) {
        const QString &imgUrl = imageUrls.at(i);

        QNetworkRequest req{QUrl{imgUrl}};
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (compatible; WebsiteEmpire/1.0)"));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

        QNetworkReply *reply = nam->get(req);
        // Capture i and imgUrl by value so each reply reports to its own slot,
        // preserving URL order no matter which reply finishes first.
        QObject::connect(reply, &QNetworkReply::finished, reply,
                         [reply, collector, i, imgUrl]() {
                             if (reply->error() == QNetworkReply::NoError) {
                                 auto img = QSharedPointer<QImage>::create();
                                 if (img->loadFromData(reply->readAll())) {
                                     collector->setResult(i, img);
                                 } else {
                                     collector->setFailure(i, imgUrl);
                                 }
                             } else {
                                 collector->setFailure(i, imgUrl);
                             }
                             reply->deleteLater();
                         });
    }
}

} // namespace OrderedImageDownloader
