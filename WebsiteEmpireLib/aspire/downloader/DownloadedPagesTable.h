#ifndef DOWNLOADEDPAGESTABLE_H
#define DOWNLOADEDPAGESTABLE_H

#include <QHash>
#include <QList>
#include <QScopedPointer>
#include <QSet>
#include <QSqlTableModel>

#include "aspire/AspiredDb.h"
#include "aspire/attributes/AbstractPageAttributes.h"

class AbstractDownloader;
class QDir;
class QImage;

// QSqlTableModel backed by the AspiredDb SQLite database for a given downloader.
//
// Writes go exclusively through recordPage() → AspiredDb::record(), which
// validates attributes and serialises images as BLOBs.  The model's own
// QSqlDatabase connection is read-only from the model's point of view:
// select() is called after each recordPage() to refresh the view.
//
// Image columns (isImage == true) are hidden from Qt::DisplayRole /
// Qt::EditRole so that views do not attempt to render raw BLOB bytes.
// Use imagesAt() to retrieve the decoded images for a given row.
class DownloadedPagesTable : public QSqlTableModel
{
    Q_OBJECT

public:
    // Opens (or creates) the database for downloader->getId() inside workingDir
    // and immediately loads the existing rows.  Throws ExceptionWithTitleText
    // if the database cannot be opened.
    explicit DownloadedPagesTable(const QDir &workingDir,
                                  AbstractDownloader *downloader,
                                  QObject *parent = nullptr);

    ~DownloadedPagesTable() override;

    // Validates idAttr_value / idAttr_imageValue via the downloader's page-
    // attributes schema, inserts the row through AspiredDb, then refreshes
    // the model with select().  Throws ExceptionWithTitleText on any
    // validation error; the model is left unchanged in that case.
    void recordPage(const QHash<QString, QString> &idAttr_value,
                    const QHash<QString, QList<QSharedPointer<QImage>>> &idAttr_imageValue = {});

    // Returns the downloader that owns this table's schema and crawl state.
    // Non-owning pointer; valid as long as the downloader passed to the
    // constructor is alive.
    AbstractDownloader *downloader() const;

    // Returns the decoded images stored in every image-type attribute of the
    // row identified by index.  Keys are attribute ids; the QList<QImage> is
    // never null (but may be empty if no images were recorded).
    // Returns an empty hash for an invalid index.
    QHash<QString, QSharedPointer<QList<QImage>>> imagesAt(const QModelIndex &index) const;

    // Returns QVariant{} for Qt::DisplayRole and Qt::EditRole on image columns
    // so that views skip rendering raw BLOB data.  All other roles and non-
    // image columns delegate to QSqlTableModel::data().
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

private:
    AspiredDb m_aspiredDb;
    AbstractDownloader *m_downloader;                       // non-owning
    QScopedPointer<AbstractPageAttributes> m_pageAttributes;
    QList<AbstractPageAttributes::Attribute> m_attributes;
    QSet<QString> m_imageAttributeIds;
};

#endif // DOWNLOADEDPAGESTABLE_H
