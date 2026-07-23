#ifndef ORDEREDIMAGECOLLECTOR_H
#define ORDEREDIMAGECOLLECTOR_H

#include <functional>

#include <QImage>
#include <QList>
#include <QSharedPointer>
#include <QString>
#include <QStringList>

// Collects images fetched in parallel while preserving the ORIGINAL URL order,
// independently of the order in which the network replies actually complete.
//
// Bug history (do not regress):
//   Images used to be appended to a shared list in network-COMPLETION order, so
//   the stored order was a permutation of the source order — the source's first
//   image could end up last, and the "primary" image was whichever reply
//   happened to finish first. A failed decode was also silently dropped, which
//   shifted every later image's position. Both violate the design lessons
//   "position is identity" (§XV-2) and "no invisible failure" (§XV-1).
//
// Contract:
//   - Construct with the number of URLs N and a completion callback.
//   - For every URL index i in [0, N), call EXACTLY ONE of setResult(i, img) or
//     setFailure(i, url), in ANY order (replies finish out of order).
//   - After the N-th call, onComplete is invoked exactly ONCE with the
//     successfully decoded images in ascending index order (i.e. URL order).
//     Failed slots are removed while the relative order of the remaining images
//     is preserved (no positional shift, no fabricated placeholder).
//
// Minimum-size policy (approved 2026-07: a product must NOT be dropped just
// because one of its images is too small — drop only the offending image):
//   - When minImageSidePx > 0, any decoded image whose smallest side is below
//     that threshold is DROPPED from the completed list, compacted out exactly
//     like a failed slot (no positional shift), with a VISIBLE qWarning naming
//     the image URL/index and the product URL (§XV-1 "no invisible failure").
//     The valid images keep their original relative order and the product is
//     recorded with them.
//   - If, AFTER filtering, ZERO valid images remain, onComplete still fires with
//     an empty list (the caller/validator then skips the product) but a clear
//     qWarning states the product has no image >= the threshold — never silent.
//   - minImageSidePx == 0 disables size filtering entirely (used by the pure
//     ordering unit tests, whose tag images are intentionally 1 px tall).
//
// This class is intentionally free of any network dependency so the
// order-preservation logic — the actual bug fix — is unit-testable by injecting
// completions out of order.
class OrderedImageCollector
{
public:
    using CompletionCallback = std::function<void(QList<QSharedPointer<QImage>>)>;

    // count            — number of URL slots (authoritative for sizing).
    // minImageSidePx   — smallest-side threshold; images below it are dropped
    //                    with a visible warning. 0 disables size filtering.
    // imageUrls        — logging context, indexed by slot; may be empty. When
    //                    present, imageUrls.size() must equal count.
    // productUrl       — logging context for the owning product page; may be empty.
    OrderedImageCollector(int count,
                          CompletionCallback onComplete,
                          int minImageSidePx = 0,
                          const QStringList &imageUrls = {},
                          const QString &productUrl = {});

    // Records a successfully decoded image at its URL position.
    void setResult(int index, const QSharedPointer<QImage> &image);

    // Records a failed download/decode at its URL position: logs a visible
    // qWarning (URL + index) and leaves the slot empty so it is compacted out
    // without shifting the successful images. Never fabricates a placeholder.
    void setFailure(int index, const QString &url);

private:
    // Shared tail for both outcomes: advance the counter and finalize when the
    // last reply has been accounted for.
    void advance();

    // Image URL recorded for slot i, or a fallback string when unknown — used
    // only to build visible warnings.
    QString urlForIndex(int index) const;

    QList<QSharedPointer<QImage>> m_slots; // pre-sized to N; null == failed/pending
    int m_remaining;                       // replies still awaited
    bool m_finished = false;               // guards against a double completion
    CompletionCallback m_onComplete;
    int m_minImageSidePx;                  // 0 == size filtering disabled
    QStringList m_imageUrls;               // logging context, indexed by slot
    QString m_productUrl;                  // logging context for the product page
};

#endif // ORDEREDIMAGECOLLECTOR_H
