#ifndef PAGEBLOCIMAGELINKSWIDGET_H
#define PAGEBLOCIMAGELINKSWIDGET_H

#include "AbstractPageBlockWidget.h"

#include <QStringList>

class ImageWriter;
namespace Ui { class PageBlocImageLinksWidget; }

/**
 * Edit widget for a PageBlocImageLinks bloc.
 *
 * Presents spinboxes for responsive grid settings (columns/rows per breakpoint)
 * and a table for editing the image-link items (image URL, link type, link
 * target, alt text).
 *
 * The "Link Type" column uses a QComboBox per row (Category / Page / URL).
 *
 * load() reads grid settings and items from the hash; save() writes them back.
 *
 * Image upload:
 *   Call setImageContext() after construction to supply an ImageWriter and the
 *   list of domains to register images against.  The "Upload Image" button is
 *   disabled until a valid context is set.  On upload, the image blob is written
 *   once via ImageWriter::writeQImage() and linked to every domain via
 *   ImageWriter::linkName().  The resulting imageUrl cell is populated with the
 *   "imgdb:<filename>" convention; PageBlocImageLinks::addCode() resolves this
 *   to "/images/<domain>/<filename>" at generation time.
 */
class PageBlocImageLinksWidget : public AbstractPageBlockWidget
{
    Q_OBJECT

public:
    explicit PageBlocImageLinksWidget(QWidget *parent = nullptr);
    ~PageBlocImageLinksWidget() override;

    void load(const QHash<QString, QString> &values) override;
    void save(QHash<QString, QString> &values) const override;

    void setImageContext(ImageWriter *imageWriter, const QStringList &domains) override;

private slots:
    void _onUploadImage();

private:
    Ui::PageBlocImageLinksWidget *ui;
    ImageWriter                  *m_imageWriter = nullptr;
    QStringList                   m_domains;

    /**
     * Appends a new row to the table with an empty image URL, a QComboBox
     * for link type (defaulting to "Category"), empty link target and alt text.
     * Returns the index of the new row.
     */
    int addRow();
};

#endif // PAGEBLOCIMAGELINKSWIDGET_H
