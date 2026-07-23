#ifndef ORDEREDIMAGEDOWNLOADER_H
#define ORDEREDIMAGEDOWNLOADER_H

#include <functional>

#include <QImage>
#include <QList>
#include <QSharedPointer>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;

// Downloads a list of image URLs in parallel and returns the decoded images in
// the SAME order as the URLs, regardless of the order in which the network
// replies complete. This is the single shared implementation used by every
// crawl/record/reparse path (§XV-4: hoist duplicated logic).
//
// Ordering, failures AND the minimum-image-size policy are delegated to
// OrderedImageCollector:
//   - each reply's result is placed at the index of its URL (position == identity);
//   - a network error or a failed QImage::loadFromData is logged (qWarning, with
//     URL + index) and its slot is dropped without shifting the other images;
//   - an image whose smallest side is below PageAttributesProduct::MIN_IMAGE_SIDE_PX
//     is likewise dropped with a visible qWarning (the product is kept with its
//     remaining valid images — approved 2026-07);
//   - onComplete runs exactly once, after every reply has finished, with the
//     compacted, order-preserved, size-filtered image list (possibly empty, in
//     which case the caller/validator skips the product with an explicit reason).
//
// productUrl is passed purely for visible logging context when an image is
// dropped or a product ends up with no valid image.
//
// Lifetime: nam must outlive the in-flight requests. Each reply is parented to
// itself and deleteLater()'d on completion (matching the existing crawl
// lifetime model); destroying nam cancels the pending replies so onComplete
// will not fire after the owning object is gone.
//
// Precondition: imageUrls must be non-empty. The "product has no image URLs"
// case is handled by the caller (which records a placeholder) and is
// intentionally NOT this function's responsibility.
namespace OrderedImageDownloader {

void fetchInOrder(QNetworkAccessManager *nam,
                  const QStringList &imageUrls,
                  const QString &productUrl,
                  std::function<void(QList<QSharedPointer<QImage>>)> onComplete);

} // namespace OrderedImageDownloader

#endif // ORDEREDIMAGEDOWNLOADER_H
