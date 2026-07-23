#include "OrderedImageCollector.h"

#include <utility>

#include <QDebug>
#include <QtGlobal>

OrderedImageCollector::OrderedImageCollector(int count,
                                             CompletionCallback onComplete,
                                             int minImageSidePx,
                                             const QStringList &imageUrls,
                                             const QString &productUrl)
    : m_remaining(count)
    , m_onComplete(std::move(onComplete))
    , m_minImageSidePx(minImageSidePx)
    , m_imageUrls(imageUrls)
    , m_productUrl(productUrl)
{
    // One slot per URL, default-constructed to a null QSharedPointer<QImage>.
    // A null slot means "pending or failed" and is compacted out at completion.
    m_slots.resize(count);
}

QString OrderedImageCollector::urlForIndex(int index) const
{
    if (index >= 0 && index < m_imageUrls.size()) {
        return m_imageUrls.at(index);
    }
    return QStringLiteral("<unknown url>");
}

void OrderedImageCollector::setResult(int index, const QSharedPointer<QImage> &image)
{
    if (index >= 0 && index < m_slots.size()) {
        m_slots[index] = image;
    } else {
        qWarning() << "OrderedImageCollector::setResult: index out of range" << index
                   << "(slot count" << m_slots.size() << ")";
    }
    advance();
}

void OrderedImageCollector::setFailure(int index, const QString &url)
{
    // Surface the failure loudly and explicitly (§XV-1): never swallow it.
    qWarning() << "OrderedImageCollector: image download/decode FAILED — index"
               << index << "url" << url
               << "(slot left empty; successful images keep their order)";
    if (index < 0 || index >= m_slots.size()) {
        qWarning() << "OrderedImageCollector::setFailure: index out of range" << index
                   << "(slot count" << m_slots.size() << ")";
    }
    // Slot stays null on purpose; advance() still accounts for this reply.
    advance();
}

void OrderedImageCollector::advance()
{
    --m_remaining;
    if (m_remaining > 0 || m_finished) {
        return;
    }
    m_finished = true;

    // Compact: keep only decoded images, preserving ascending index (URL) order.
    // When size filtering is enabled, an image whose smallest side is below the
    // threshold is dropped here EXACTLY like a failed slot — the surviving images
    // keep their relative order and no placeholder is fabricated. Each drop is
    // surfaced loudly so it is never an invisible failure (§XV-1).
    QList<QSharedPointer<QImage>> ordered;
    ordered.reserve(m_slots.size());
    for (int i = 0; i < m_slots.size(); ++i) {
        const QSharedPointer<QImage> &slot = m_slots.at(i);
        if (!slot) {
            continue; // failed or never-set slot (already accounted for)
        }
        if (m_minImageSidePx > 0) {
            const int minSide = qMin(slot->width(), slot->height());
            if (minSide < m_minImageSidePx) {
                qWarning() << "OrderedImageCollector: image DROPPED (below minimum"
                           << "size) — index" << i
                           << "size" << slot->width() << "x" << slot->height()
                           << "min side" << minSide << "< required" << m_minImageSidePx
                           << "px; image url" << urlForIndex(i)
                           << "product url" << m_productUrl
                           << "(product kept with its remaining valid images)";
                continue;
            }
        }
        ordered.append(slot);
    }

    // Filtering (or all-failed downloads) may leave nothing valid. Say so
    // explicitly — the caller/validator then skips the product, but never silently.
    if (m_minImageSidePx > 0 && ordered.isEmpty() && !m_slots.isEmpty()) {
        qWarning() << "OrderedImageCollector: product has NO image >="
                   << m_minImageSidePx << "px after filtering "
                   << m_slots.size() << "downloaded image(s) — product will be"
                   << "SKIPPED (no valid image); product url" << m_productUrl;
    }

    if (m_onComplete) {
        m_onComplete(ordered);
    }
}
