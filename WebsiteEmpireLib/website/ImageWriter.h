#ifndef IMAGEWRITER_H
#define IMAGEWRITER_H

#include <QDir>
#include <QString>

class QByteArray;
class QImage;

/**
 * Qt-side write path for images.db.
 *
 * Schema (mirrors StaticWebsiteServeLib/db/ImageDb):
 *   images      — stores unique blobs: id, blob, mime_type
 *   image_names — domain + filename → image_id indirection
 *
 * The indirection is the key to both IMGFIX and IMGTR use cases:
 *
 *   IMGFIX (same image, multiple domains/languages):
 *     - Call writeQImage() or writeSvg() once to get an image_id.
 *     - Call linkName() once per (domain, filename) pair to register
 *       the same blob under different filenames across languages.
 *
 *   IMGTR (translated image — different blobs per language):
 *     - Call writeQImage() / writeSvg() once per language variant.
 *       Each call inserts a distinct blob row and returns a distinct image_id.
 *     - Call linkName() with the per-language (domain, filename) for each.
 *
 * SVG images are stored as raw UTF-8 bytes with mime_type "image/svg+xml".
 * Raster images are encoded as WebP blobs with mime_type "image/webp".
 *
 * The database file is opened for the lifetime of this object.
 * Holds no reference to any other Qt object.
 */
class ImageWriter
{
public:
    static constexpr const char *FILENAME = "images.db";

    explicit ImageWriter(const QDir &workingDir);
    ~ImageWriter();

    /**
     * Inserts a raw blob into the images table and returns its image_id.
     * Does not check for duplicate blobs — call linkName() separately if
     * the same blob should be served under additional (domain, filename) pairs.
     */
    qint64 writeImage(const QByteArray &blob, const QString &mimeType);

    /**
     * Upserts a (domain, filename) → imageId row in image_names.
     * If the (domain, filename) pair already exists its image_id is updated.
     * domain   — bare hostname, e.g. "example.com"
     * filename — URL filename without leading slash, e.g. "hero.webp"
     */
    void linkName(qint64 imageId, const QString &domain, const QString &filename);

    /**
     * Converts image to WebP, writes the blob, and registers the name.
     * Logs a warning and returns -1 if WebP encoding fails (WebP Qt plugin missing).
     * Wraps writeImage() + linkName() in a transaction.
     * Returns the image_id.
     */
    qint64 writeQImage(const QImage  &image,
                       const QString &domain,
                       const QString &filename);

    /**
     * Writes raw SVG bytes with mime_type "image/svg+xml" and registers the name.
     * Wraps writeImage() + linkName() in a transaction.
     * Returns the image_id.
     */
    qint64 writeSvg(const QByteArray &svgData,
                    const QString    &domain,
                    const QString    &filename);

private:
    void _ensureSchema();

    QString m_connName;
};

#endif // IMAGEWRITER_H
