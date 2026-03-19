#include "WidgetDownloader.h"
#include "ui_WidgetDownloader.h"

#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QItemSelectionModel>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPointer>
#include <QPromise>
#include <QSettings>
#include <QShortcut>
#include <QTextStream>

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
    ui->buttonDownloadFromFileUrls->setEnabled(false);
    ui->buttonDownloadFromFileUrls->setCheckable(true);
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
    connect(ui->buttonCopyUrl,
            &QPushButton::clicked,
            this,
            &WidgetDownloader::copyUrl);
    connect(ui->buttonDownloadFromFileUrls,
            &QPushButton::clicked,
            this,
            &WidgetDownloader::downloadFromFileUrls);

    auto *copyShortcut = new QShortcut(
        QKeySequence::Copy, ui->tableViewPages, nullptr, nullptr, Qt::WidgetShortcut);
    connect(copyShortcut, &QShortcut::activated, this, &WidgetDownloader::copyUrl);
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

    ui->buttonDownloadFromFileUrls->setEnabled(
        dowanloadedPageTable->downloader()->supportsFileUrlDownload());
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

// ---------------------------------------------------------------------------
// _buildPageParsedCallback
// ---------------------------------------------------------------------------

std::function<QFuture<bool>(const QString &, const QHash<QString, QString> &)>
WidgetDownloader::_buildPageParsedCallback()
{
    // Capture everything by value / safe pointer so that the lambda does not
    // dangle if WidgetDownloader is destroyed mid-crawl.
    QPointer<WidgetDownloader> self(this);
    const QString imageUrlKey = m_dowanloadedPageTable->downloader()->getImageUrlAttributeKey();

    return [self, imageUrlKey](const QString & /*url*/,
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

        // --- Fetch product images asynchronously (no nested event loop) ---
        // imageUrlKey may hold a semicolon-separated list of URLs; download all
        // in parallel and call record() once every reply has been received.
        const QStringList imageUrls = attrs.value(imageUrlKey)
                                          .split(QLatin1Char(';'), Qt::SkipEmptyParts);
        if (!imageUrls.isEmpty() && self->m_nam) {
            auto collected = QSharedPointer<QList<QSharedPointer<QImage>>>::create();
            auto remaining = QSharedPointer<int>::create(imageUrls.size());

            for (const QString &imgUrl : imageUrls) {
                QNetworkRequest req{QUrl{imgUrl}};
                req.setHeader(QNetworkRequest::UserAgentHeader,
                              QStringLiteral("Mozilla/5.0 (compatible; WebsiteEmpire/1.0)"));
                req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::NoLessSafeRedirectPolicy);

                QNetworkReply *reply = self->m_nam->get(req);
                QObject::connect(reply, &QNetworkReply::finished, reply,
                                 [self, reply, collected, remaining, record]() mutable {
                                     if (self) {
                                         auto img = QSharedPointer<QImage>::create();
                                         if (img->loadFromData(reply->readAll())) {
                                             *collected << img;
                                         }
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
            // No image URLs: record with a placeholder immediately.
            auto placeholder = QSharedPointer<QImage>::create(200, 200, QImage::Format_RGB32);
            placeholder->fill(Qt::white);
            record({placeholder});
        }

        return promise->future();
    };
}

// ---------------------------------------------------------------------------
// download
// ---------------------------------------------------------------------------

void WidgetDownloader::download(bool start)
{
    ui->progressBar->setVisible(start);
    if (!m_dowanloadedPageTable) {
        ui->progressBar->hide();
        return;
    }

    if (!start) {
        disconnect(m_downloadFinishedConnection);
        m_stopDownload = true;
        m_dowanloadedPageTable->downloader()->requestStop();
        ui->buttonDownload->setChecked(false);
        ui->buttonDownload->setText(tr("Download"));
        ui->buttonDownloadFromFileUrls->setEnabled(
            m_dowanloadedPageTable->downloader()->supportsFileUrlDownload());
        ui->progressBar->hide();
        return;
    }
    ui->progressBar->setTextVisible(true);

    m_stopDownload = false;
    m_sessionRecordCount = 0;
    ui->buttonDownload->setText(tr("Stop"));
    ui->buttonDownloadFromFileUrls->setEnabled(false);

    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
    }

    AbstractDownloader *dl = m_dowanloadedPageTable->downloader();
    dl->setPageParsedCallback(_buildPageParsedCallback());

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
            ui->buttonDownloadFromFileUrls->setEnabled(
                m_dowanloadedPageTable->downloader()->supportsFileUrlDownload());
            ui->labelDownloadQuantities->setText(
                ui->labelDownloadQuantities->text()
                + tr(" — %1").arg(tr("FINISHED")));
        });

    // Start (or resume) the crawl; pass seed URLs only on first launch — the
    // downloader's .ini keeps the queue across sessions automatically.
    m_parseFuture = dl->parse(dl->getSeedUrls());
}

// ---------------------------------------------------------------------------
// downloadFromFileUrls
// ---------------------------------------------------------------------------

void WidgetDownloader::downloadFromFileUrls(bool start)
{
    if (!m_dowanloadedPageTable) {
        ui->buttonDownloadFromFileUrls->setChecked(false);
        return;
    }

    if (!start) {
        disconnect(m_downloadFinishedConnection);
        m_stopDownload = true;
        m_dowanloadedPageTable->downloader()->requestStop();
        ui->buttonDownloadFromFileUrls->setChecked(false);
        ui->buttonDownloadFromFileUrls->setText(tr("Download From File URLs"));
        ui->buttonDownload->setEnabled(true);
        ui->progressBar->hide();
        return;
    }

    // --- Pick the URL list file ---
    QSettings appSettings;
    appSettings.beginGroup(QStringLiteral("FileUrlsDownloader"));
    const QString lastFolder = appSettings.value(QStringLiteral("lastFolder"),
                                                  QDir::homePath()).toString();

    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Select URL List"),
        lastFolder,
        tr("Text files (*.txt);;All files (*)"));

    if (filePath.isEmpty()) {
        ui->buttonDownloadFromFileUrls->setChecked(false);
        appSettings.endGroup();
        return;
    }

    appSettings.setValue(QStringLiteral("lastFolder"), QFileInfo(filePath).absolutePath());
    appSettings.endGroup();

    // --- Read one URL per line, skip empty lines ---
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this,
                             tr("File Error"),
                             tr("Cannot open file: %1").arg(filePath));
        ui->buttonDownloadFromFileUrls->setChecked(false);
        return;
    }

    QStringList urls;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (!line.isEmpty()) {
            urls.append(line);
        }
    }

    if (urls.isEmpty()) {
        QMessageBox::information(this,
                                 tr("Empty File"),
                                 tr("No URLs found in the selected file."));
        ui->buttonDownloadFromFileUrls->setChecked(false);
        return;
    }

    // --- Start download ---
    ui->progressBar->setVisible(true);
    ui->progressBar->setTextVisible(true);
    m_stopDownload = false;
    m_sessionRecordCount = 0;
    ui->buttonDownloadFromFileUrls->setText(tr("Stop"));
    ui->buttonDownload->setEnabled(false);

    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
    }

    AbstractDownloader *dl = m_dowanloadedPageTable->downloader();
    dl->setPageParsedCallback(_buildPageParsedCallback());

    disconnect(m_downloadFinishedConnection);
    m_downloadFinishedConnection = connect(
        dl, &AbstractDownloader::finished,
        this, [this]() {
            m_stopDownload = true;
            ui->progressBar->hide();
            ui->buttonDownloadFromFileUrls->setChecked(false);
            ui->buttonDownloadFromFileUrls->setText(tr("Download From File URLs"));
            ui->buttonDownload->setEnabled(true);
            ui->labelDownloadQuantities->setText(
                ui->labelDownloadQuantities->text()
                + tr(" — %1").arg(tr("FINISHED")));
        });

    m_parseFuture = dl->parseSpecificUrls(urls);
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

            const QStringList imageUrls = attrs.value(imageUrlKey)
                                              .split(QLatin1Char(';'), Qt::SkipEmptyParts);
            if (!imageUrls.isEmpty() && self->m_nam) {
                auto collected = QSharedPointer<QList<QSharedPointer<QImage>>>::create();
                auto remaining = QSharedPointer<int>::create(imageUrls.size());

                for (const QString &imgUrl : imageUrls) {
                    QNetworkRequest req{QUrl{imgUrl}};
                    req.setHeader(QNetworkRequest::UserAgentHeader,
                                  QStringLiteral("Mozilla/5.0 (compatible; WebsiteEmpire/1.0)"));
                    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                     QNetworkRequest::NoLessSafeRedirectPolicy);

                    QNetworkReply *reply = self->m_nam->get(req);
                    QObject::connect(reply, &QNetworkReply::finished, reply,
                                     [self, reply, collected, remaining, doUpdate]() mutable {
                                         if (self) {
                                             auto img = QSharedPointer<QImage>::create();
                                             if (img->loadFromData(reply->readAll())) {
                                                 *collected << img;
                                             }
                                         }
                                         reply->deleteLater();
                                         if (--(*remaining) == 0) {
                                             if (collected->isEmpty()) {
                                                 auto placeholder = QSharedPointer<QImage>::create(
                                                     200, 200, QImage::Format_RGB32);
                                                 placeholder->fill(Qt::white);
                                                 *collected << placeholder;
                                             }
                                             doUpdate(std::move(*collected));
                                         }
                                     });
                }
            } else {
                auto placeholder = QSharedPointer<QImage>::create(200, 200, QImage::Format_RGB32);
                placeholder->fill(Qt::white);
                doUpdate({placeholder});
            }

            return promise->future();
        };

        dl->reparseUrl(url, reparseCallback);
    }
}

void WidgetDownloader::copyUrl()
{
    if (!m_dowanloadedPageTable) {
        return;
    }

    const QModelIndexList selected =
        ui->tableViewPages->selectionModel()->selectedRows();

    if (selected.isEmpty()) {
        return;
    }

    // Find the URL attribute id in the schema.
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
                             tr("No URL attribute found in the schema."));
        return;
    }

    // Find the column index for the URL attribute.
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
                             tr("No URL Attribute"),
                             tr("No URL attribute found in the schema."));
        return;
    }

    // Use EditRole to get the full URL (DisplayRole may truncate).
    const QModelIndex &firstIndex = selected.first();
    const QString url =
        m_dowanloadedPageTable->data(firstIndex.siblingAtColumn(urlColIndex), Qt::EditRole)
            .toString();

    if (url.isEmpty()) {
        return;
    }

    QGuiApplication::clipboard()->setText(url);
    ui->labelLog->setText(tr("URL copied: %1").arg(url));
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
