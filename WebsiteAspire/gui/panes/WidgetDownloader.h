#ifndef WIDGETDOWNLOADER_H
#define WIDGETDOWNLOADER_H

#include <QFuture>
#include <QItemSelection>
#include <QWidget>

class AbstractPageAttributes;
class DownloadedPagesTable;
class QListWidgetItem;
class QNetworkAccessManager;

namespace Ui {
class WidgetDownloader;
}

class WidgetDownloader : public QWidget
{
    Q_OBJECT

public:
    explicit WidgetDownloader(QWidget *parent = nullptr);

    // Binds the widget to a page-attributes schema and a live table model.
    // Must be called before any user interaction.  Neither pointer is owned.
    void init(const AbstractPageAttributes *pageAttribute,
              DownloadedPagesTable *dowanloadedPageTable);

    ~WidgetDownloader();

public slots:
    // Opens a read-only dialog showing the downloader's attribute schema.
    void viewAttributes();

    // start == true  → begin (or resume) an async crawl and record into the table.
    // start == false → signal the crawl to stop; already-downloaded rows are kept.
    void download(bool start);
    void removePages();
    void reparse();
    void copyUrl();
    void copyCommand();

private slots:
    void _onRowAttributeSelected(const QItemSelection &selected, const QItemSelection &deselected);
    void _onImageItemClicked(QListWidgetItem *item);

private:
    void _connectSlots();

    Ui::WidgetDownloader *ui;
    const AbstractPageAttributes *m_pageAttribute = nullptr;
    DownloadedPagesTable *m_dowanloadedPageTable = nullptr;

    // Lazily initialised on first download() call.
    QNetworkAccessManager *m_nam = nullptr;

    // Set to true by download(false) so the running callback stops recording.
    bool m_stopDownload = false;

    // Number of pages recorded in the current download session (reset on each
    // download(true) call).
    int m_sessionRecordCount = 0;

    // Tracks the running parse() future (kept alive for the duration of the crawl).
    QFuture<void> m_parseFuture;

    // Connection to AbstractDownloader::finished() — disconnected on user stop
    // and replaced on each new download(true) call.
    QMetaObject::Connection m_downloadFinishedConnection;
};

#endif // WIDGETDOWNLOADER_H
