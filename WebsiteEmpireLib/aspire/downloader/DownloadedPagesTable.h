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
// Image columns (isImage == true) display "N images" in Qt::DisplayRole /
// Qt::EditRole instead of raw BLOB bytes.  Only the 4-byte count header is
// fetched during select(); full pixel data is retrieved on-demand via imagesAt().
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

    // Downloader-free constructor: uses tableId as the database filename stem and
    // takes ownership of pageAttributes.  downloader() returns nullptr for instances
    // created with this overload.  Useful for generator result tables where no
    // AbstractDownloader exists.
    explicit DownloadedPagesTable(const QDir &workingDir,
                                  const QString &tableId,
                                  AbstractPageAttributes *pageAttributes,
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

    // Returns "N images" for Qt::DisplayRole and Qt::EditRole on image columns
    // (parsed from the 4-byte count header stored by select()).  All other
    // roles and non-image columns delegate to QSqlTableModel::data().
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Returns Qt::ItemIsEditable for non-image cells so that double-clicking
    // opens an editor and the cell value can be copied.  Image cells are not
    // editable because they hold raw BLOB data.
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // Always returns false without modifying any data.  Edits initiated through
    // the view (e.g. after double-click) must not persist; all writes go through
    // recordPage() → AspiredDb::record().
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

    // Like recordPage(), but updates the existing row identified by rowId
    // (the auto-increment primary key from column 0) instead of inserting a
    // new one.  Validates via the schema before writing.  Throws
    // ExceptionWithTitleText on validation failure; the row is left unchanged.
    void updatePage(const QString &rowId,
                    const QHash<QString, QString> &idAttr_value,
                    const QHash<QString, QList<QSharedPointer<QImage>>> &idAttr_imageValue = {});

    // Deletes the rows whose primary-key ids are listed in rowIds, then
    // refreshes the model with select().  Rows not found in the database are
    // silently ignored.  Throws ExceptionWithTitleText on SQL error.
    void deleteRows(const QList<QString> &rowIds);

    // Overrides the default SELECT * to exclude image (BLOB) columns.
    // Loading large BLOBs eagerly during select() blocks the main thread;
    // images are retrieved on-demand via imagesAt() instead.
    bool select() override;

private:
    AspiredDb m_aspiredDb;
    AbstractDownloader *m_downloader;                       // non-owning
    QScopedPointer<AbstractPageAttributes> m_pageAttributes;
    QList<AbstractPageAttributes::Attribute> m_attributes;
    QSet<QString> m_imageAttributeIds;
};

#endif // DOWNLOADEDPAGESTABLE_H
