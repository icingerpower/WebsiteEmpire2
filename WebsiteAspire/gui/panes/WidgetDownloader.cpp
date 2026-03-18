#include "WidgetDownloader.h"
#include "ui_WidgetDownloader.h"

#include <QMessageBox>
#include <QImage>
#include <QItemSelectionModel>
#include <QListWidgetItem>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPointer>
#include <QPromise>

#include "gui/dialog/DialogViewAttributes.h"

#include "aspire/attributes/AbstractPageAttributes.h"
#include "workingdirectory/WorkingDirectoryManager.h"
#include "aspire/attributes/PageAttributesProduct.h"
#include "aspire/downloader/AbstractDownloader.h"
#include "aspire/downloader/DownloadedPagesTable.h"
#include "launcher/AbstractLauncher.h"
#include "launcher/LauncherDownload.h"

WidgetDownloader::WidgetDownloader(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::WidgetDownloader)
{
    ui->setupUi(this);
    ui->buttonDownload->setEnabled(false);
    ui->buttonDownload->setCheckable(true);
    ui->progressBar->hide();
    _connectSlots();
}

void WidgetDownloader::_connectSlots()
{
    connect(ui->buttonDownload,
            &QPushButton::clicked,
            this,
            &WidgetDownloader::download);
    connect(ui->buttonViewAttributes,
            &QPushButton::clicked,
            this,
            &WidgetDownloader::viewAttributes);
    connect(ui->buttonRemovePages,
            &QPushButton::clicked,
            this,
            &WidgetDownloader::removePages);
    connect(ui->listWidgetImages,
            &QListWidget::itemClicked,
            this,
            &WidgetDownloader::_onImageItemClicked);
    connect(ui->buttonReparse,
            &QPushButton::clicked,
            this,
            &WidgetDownloader::reparse);
    connect(ui->buttonCopyCommand,
            &QPushButton::clicked,
            this,
            &WidgetDownloader::copyCommand);
}

void WidgetDownloader::init(const AbstractPageAttributes *pageAttribute,
                            DownloadedPagesTable *dowanloadedPageTable)
{
    ui->buttonDownload->setEnabled(true);
    m_pageAttribute = pageAttribute;
    m_dowanloadedPageTable = dowanloadedPageTable;

    ui->tableViewPages->setModel(m_dowanloadedPageTable);

    connect(ui->tableViewPages->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this,
            &WidgetDownloader::_onRowAttributeSelected);

    ui->labelImage->setAlignment(Qt::AlignCenter);
    ui->labelDownloadQuantities->setText(tr("%1 pages").arg(m_dowanloadedPageTable->rowCount()));
}

WidgetDownloader::~WidgetDownloader()
{
    // Clear the callback so the running parse() loop does not call into a
    // destroyed WidgetDownloader.
    if (m_dowanloadedPageTable) {
        m_dowanloadedPageTable->downloader()->setPageParsedCallback({});
    }
    delete ui;
}

// ---------------------------------------------------------------------------
// viewAttributes
// ---------------------------------------------------------------------------

void WidgetDownloader::viewAttributes()
{
    if (!m_pageAttribute) {
        return;
    }
    DialogViewAttributes dialog(m_pageAttribute, this);
    dialog.exec();
}

// ---------------------------------------------------------------------------
// download
// ---------------------------------------------------------------------------

void WidgetDownloader::download(bool start)
{
    if (!m_dowanloadedPageTable) {
        return;
    }

    if (!start) {
        disconnect(m_downloadFinishedConnection);
        m_stopDownload = true;
        ui->buttonDownload->setChecked(false);
        ui->buttonDownload->setText(tr("Download"));
        return;
    }
    ui->progressBar->show();
    ui->progressBar->setTextVisible(start);

    m_stopDownload = false;
    m_sessionRecordCount = 0;
    ui->buttonDownload->setText(tr("Stop"));

    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
    }

    AbstractDownloader *dl = m_dowanloadedPageTable->downloader();
    const QString imageUrlKey = dl->getImageUrlAttributeKey();

    // Capture everything the callback needs by value / safe pointer so that
    // the lambda does not dangle if WidgetDownloader is destroyed mid-crawl.
    QPointer<WidgetDownloader> self(this);

    dl->setPageParsedCallback(
        [self, imageUrlKey](const QString & /*url*/,
                            const QHash<QString, QString> &attrs) -> QFuture<bool> {
            auto promise = QSharedPointer<QPromise<bool>>::create();
            promise->start();

            if (!self || self->m_stopDownload || attrs.isEmpty()) {
                promise->addResult(false);
                promise->finish();
                return promise->future();
            }

            // Helper: records the page with the given images and resolves the promise.
            auto record = [self, attrs, imageUrlKey, promise](QList<QSharedPointer<QImage>> images) {
                // Strip the image-URL transport key before handing attrs to the schema.
                QHash<QString, QString> textAttrs = attrs;
                textAttrs.remove(imageUrlKey);

                const QHash<QString, QList<QSharedPointer<QImage>>> imageAttrs{
                    {PageAttributesProduct::ID_IMAGES, images}
                };

                try {
                    self->m_dowanloadedPageTable->recordPage(textAttrs, imageAttrs);
                    ++self->m_sessionRecordCount;
                    const int total = self->m_dowanloadedPageTable->rowCount();
                    self->ui->labelDownloadQuantities->setText(
                        QObject::tr("%1 added (total %2 pages)")
                            .arg(self->m_sessionRecordCount)
                            .arg(total));
                } catch (const QException &ex) {
                    qDebug() << "WidgetDownloader: record failed:" << ex.what();
                }

                promise->addResult(true);
                promise->finish();
            };

            // --- Fetch product image asynchronously (no nested event loop) ---
            const QString imageUrl = attrs.value(imageUrlKey);
            if (!imageUrl.isEmpty() && self->m_nam) {
                QNetworkRequest req{QUrl{imageUrl}};
                req.setHeader(QNetworkRequest::UserAgentHeader,
                              QStringLiteral("Mozilla/5.0 (compatible; WebsiteEmpire/1.0)"));
                req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::NoLessSafeRedirectPolicy);

                QNetworkReply *reply = self->m_nam->get(req);
                QObject::connect(reply, &QNetworkReply::finished, reply,
                                 [self, reply, record]() mutable {
                                     QList<QSharedPointer<QImage>> images;
                                     if (!self) {
                                         reply->deleteLater();
                                         return;
                                     }
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
                // No image URL: record with a placeholder immediately.
                auto placeholder = QSharedPointer<QImage>::create(
                    200, 200, QImage::Format_RGB32);
                placeholder->fill(Qt::white);
                record({placeholder});
            }

            return promise->future();
        });

    // Disconnect any previous finished() connection before creating a new one
    // so that restarting a stopped crawl never fires the old handler.
    disconnect(m_downloadFinishedConnection);
    m_downloadFinishedConnection = connect(
        dl, &AbstractDownloader::finished,
        this, [this]() {
            m_stopDownload = true;
            ui->progressBar->hide();
            ui->buttonDownload->setChecked(false);
            ui->buttonDownload->setText(tr("Download"));
            ui->labelDownloadQuantities->setText(
                ui->labelDownloadQuantities->text()
                + tr(" — %1").arg(tr("FINISHED")));
        });

    // Start (or resume) the crawl; pass seed URLs only on first launch — the
    // downloader's .ini keeps the queue across sessions automatically.
    m_parseFuture = dl->parse(dl->getSeedUrls());
}

void WidgetDownloader::removePages()
{
    if (!m_dowanloadedPageTable) {
        return;
    }

    const QModelIndexList selected =
        ui->tableViewPages->selectionModel()->selectedRows();

    if (selected.isEmpty()) {
        QMessageBox::information(this,
                                 tr("No Selection"),
                                 tr("Please select one or more rows to remove."));
        return;
    }

    const int count = selected.size();
    const auto answer = QMessageBox::question(
        this,
        tr("Confirm Deletion"),
        tr("Delete %n page(s)?", "", count),
        QMessageBox::Yes | QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        return;
    }

    QList<QString> ids;
    ids.reserve(count);
    for (const auto &index : std::as_const(selected)) {
        ids.append(
            m_dowanloadedPageTable->data(index.siblingAtColumn(0), Qt::DisplayRole)
                .toString());
    }

    m_dowanloadedPageTable->deleteRows(ids);
    ui->labelDownloadQuantities->setText(
        tr("%1 pages").arg(m_dowanloadedPageTable->rowCount()));
}

void WidgetDownloader::reparse()
{
    if (!m_dowanloadedPageTable) {
        return;
    }

    const QModelIndexList selected =
        ui->tableViewPages->selectionModel()->selectedRows();

    if (selected.isEmpty()) {
        QMessageBox::warning(this,
                             tr("No Selection"),
                             tr("Please select one or more rows to reparse."));
        return;
    }

    // Find the first URL attribute in the schema.
    QString urlAttrId;
    const auto &attrsPtr = m_pageAttribute->getAttributes();
    if (attrsPtr) {
        for (const auto &attr : std::as_const(*attrsPtr)) {
            if (attr.isUrl) {
                urlAttrId = attr.id;
                break;
            }
        }
    }

    if (urlAttrId.isEmpty()) {
        QMessageBox::warning(this,
                             tr("No URL Attribute"),
                             tr("No URL attribute found in the schema. Reparse is not possible."));
        return;
    }

    // Find the column index for the URL attribute in the model.
    int urlColIndex = -1;
    for (int c = 0; c < m_dowanloadedPageTable->columnCount(); ++c) {
        if (m_dowanloadedPageTable->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString()
            == urlAttrId) {
            urlColIndex = c;
            break;
        }
    }

    if (urlColIndex < 0) {
        QMessageBox::warning(this,
                             tr("URL Column Not Found"),
                             tr("The URL column is not visible in the table. Reparse is not possible."));
        return;
    }

    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
    }

    AbstractDownloader *dl = m_dowanloadedPageTable->downloader();
    const QString imageUrlKey = dl->getImageUrlAttributeKey();
    QPointer<WidgetDownloader> self(this);

    for (const auto &index : std::as_const(selected)) {
        const QString rowId =
            m_dowanloadedPageTable->data(index.siblingAtColumn(0), Qt::DisplayRole).toString();
        // Use EditRole to retrieve the full URL — DisplayRole truncates text
        // longer than 120 characters, which would corrupt the fetch request.
        const QString url =
            m_dowanloadedPageTable->data(index.siblingAtColumn(urlColIndex), Qt::EditRole).toString();

        if (url.isEmpty()) {
            continue;
        }

        // Callback that calls updatePage() instead of recordPage().
        auto reparseCallback =
            [self, rowId, imageUrlKey](const QString & /*url*/,
                                       const QHash<QString, QString> &attrs) -> QFuture<bool> {
            auto promise = QSharedPointer<QPromise<bool>>::create();
            promise->start();

            if (!self || attrs.isEmpty()) {
                promise->addResult(false);
                promise->finish();
                return promise->future();
            }

            auto doUpdate = [self, rowId, attrs, imageUrlKey, promise](
                                QList<QSharedPointer<QImage>> images) {
                QHash<QString, QString> textAttrs = attrs;
                textAttrs.remove(imageUrlKey);

                const QHash<QString, QList<QSharedPointer<QImage>>> imageAttrs{
                    {PageAttributesProduct::ID_IMAGES, images}
                };

                try {
                    self->m_dowanloadedPageTable->updatePage(rowId, textAttrs, imageAttrs);
                } catch (const QException &ex) {
                    qDebug() << "WidgetDownloader: reparse update failed:" << ex.what();
                }

                promise->addResult(true);
                promise->finish();
            };

            const QString imageUrl = attrs.value(imageUrlKey);
            if (!imageUrl.isEmpty() && self->m_nam) {
                QNetworkRequest req{QUrl{imageUrl}};
                req.setHeader(QNetworkRequest::UserAgentHeader,
                              QStringLiteral("Mozilla/5.0 (compatible; WebsiteEmpire/1.0)"));
                req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::NoLessSafeRedirectPolicy);

                QNetworkReply *reply = self->m_nam->get(req);
                QObject::connect(reply, &QNetworkReply::finished, reply,
                                 [self, reply, doUpdate]() mutable {
                                     if (!self) {
                                         reply->deleteLater();
                                         return;
                                     }
                                     const QByteArray imgData = reply->readAll();
                                     reply->deleteLater();

                                     QList<QSharedPointer<QImage>> images;
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
                                     doUpdate(std::move(images));
                                 });
            } else {
                auto placeholder = QSharedPointer<QImage>::create(
                    200, 200, QImage::Format_RGB32);
                placeholder->fill(Qt::white);
                doUpdate({placeholder});
            }

            return promise->future();
        };

        dl->reparseUrl(url, reparseCallback);
    }
}

void WidgetDownloader::copyCommand()
{
    if (!m_dowanloadedPageTable) {
        return;
    }
    const QString workingDir = WorkingDirectoryManager::instance()->workingDir().path();
    const QString downloaderId = m_dowanloadedPageTable->downloader()->getId();
    const QString command =
        QStringLiteral("WebsiteAspire --%1 \"%2\" --%3 %4")
            .arg(AbstractLauncher::OPTION_WORKING_DIR,
                 workingDir,
                 LauncherDownload::OPTION_NAME,
                 downloaderId);
    QMessageBox::information(this,
                             tr("Copy Command"),
                             tr("Run the following command in a terminal to download "
                                "without launching the UI:\n\n%1")
                                 .arg(command));
}

// ---------------------------------------------------------------------------
// Row / image selection
// ---------------------------------------------------------------------------

void WidgetDownloader::_onRowAttributeSelected(const QItemSelection &selected,
                                      const QItemSelection & /*deselected*/)
{
    ui->listWidgetImages->clear();
    ui->labelImage->clear();

    if (selected.isEmpty() || !m_dowanloadedPageTable) {
        return;
    }

    const QModelIndex index = selected.indexes().first();
    const auto allImages = m_dowanloadedPageTable->imagesAt(index);

    if (allImages.isEmpty()) {
        return;
    }

    // Use the first image-attribute's list (usually ID_IMAGES).
    const QSharedPointer<QList<QImage>> imgList = allImages.constBegin().value();
    if (!imgList || imgList->isEmpty()) {
        return;
    }

    for (const QImage &img : std::as_const(*imgList)) {
        auto *item = new QListWidgetItem(ui->listWidgetImages);
        const QPixmap thumb = QPixmap::fromImage(
            img.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        item->setIcon(QIcon(thumb));
        // Store the full pixmap for display in labelImage.
        item->setData(Qt::UserRole, QPixmap::fromImage(img));
    }

    // Show the first image immediately.
    if (ui->listWidgetImages->count() > 0) {
        _onImageItemClicked(ui->listWidgetImages->item(0));
    }
}

void WidgetDownloader::_onImageItemClicked(QListWidgetItem *item)
{
    if (!item) {
        return;
    }
    const QPixmap pm = item->data(Qt::UserRole).value<QPixmap>();
    if (pm.isNull()) {
        return;
    }
    // Scale to fill the label while keeping aspect ratio.
    ui->labelImage->setPixmap(
        pm.scaled(ui->labelImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
