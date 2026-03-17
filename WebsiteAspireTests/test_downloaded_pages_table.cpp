#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFuture>
#include <QImage>
#include <QPromise>
#include <QSharedPointer>
#include <QTemporaryDir>

#include "aspire/downloader/DownloadedPagesTable.h"
#include "aspire/downloader/AbstractDownloader.h"
#include "aspire/attributes/AbstractPageAttributes.h"
#include "ExceptionWithTitleText.h"

// ---------------------------------------------------------------------------
// Stub page-attributes: configurable list + optional cross-validator
// ---------------------------------------------------------------------------
class StubPageAttributes : public AbstractPageAttributes
{
public:
    using Attr = AbstractPageAttributes::Attribute;

    explicit StubPageAttributes(
        const QString &id,
        const QString &name,
        const QList<Attr> &attrs,
        std::function<QString(const QHash<QString, QString> &)> crossValidator = {})
        : m_id(id)
        , m_name(name)
        , m_attrs(attrs)
        , m_crossValidator(std::move(crossValidator))
    {}

    QString getId()          const override { return m_id; }
    QString getName()        const override { return m_name; }
    QString getDescription() const override { return QString{}; }

    QSharedPointer<QList<Attr>> getAttributes() const override
    {
        return QSharedPointer<QList<Attr>>::create(m_attrs);
    }

    QString areAttributesCrossValid(const QHash<QString, QString> &vals) const override
    {
        if (m_crossValidator) {
            return m_crossValidator(vals);
        }
        return AbstractPageAttributes::areAttributesCrossValid(vals);
    }

private:
    QString m_id;
    QString m_name;
    QList<Attr> m_attrs;
    std::function<QString(const QHash<QString, QString> &)> m_crossValidator;
};

// ---------------------------------------------------------------------------
// Stub downloader: never fetches anything, just provides schema
// ---------------------------------------------------------------------------
class StubDownloader : public AbstractDownloader
{
public:
    using Attr = AbstractPageAttributes::Attribute;

    explicit StubDownloader(
        const QString &id,
        const QList<Attr> &attrs,
        std::function<QString(const QHash<QString, QString> &)> crossValidator = {})
        : AbstractDownloader()
        , m_id(id)
        , m_attrs(attrs)
        , m_crossValidator(std::move(crossValidator))
    {}

    QString getId()          const override { return m_id; }
    QString getName()        const override { return m_id; }
    QString getDescription() const override { return QString{}; }

    AbstractPageAttributes *createPageAttributes() const override
    {
        return new StubPageAttributes(m_id, m_id, m_attrs, m_crossValidator);
    }

    AbstractDownloader *createInstance(const QDir &) const override { return nullptr; }
    QStringList getUrlsToParse(const QString &)         const override { return {}; }
    QHash<QString, QString> getAttributeValues(const QString &, const QString &) const override { return {}; }

protected:
    QFuture<QString> fetchUrl(const QString &) override
    {
        QPromise<QString> p;
        p.start();
        p.addResult(QString{});
        p.finish();
        return p.future();
    }

private:
    QString m_id;
    QList<Attr> m_attrs;
    std::function<QString(const QHash<QString, QString> &)> m_crossValidator;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace {

using Attr = AbstractPageAttributes::Attribute;

Attr makeTextAttr(const QString &id, const QString &name, bool required = false)
{
    return Attr{
        id, name, "", "example", "",
        [required, name](const QString &v) -> QString {
            if (required && v.trimmed().isEmpty()) {
                return name + " is required";
            }
            return QString{};
        }
    };
}

Attr makeImgAttr(const QString &id, const QString &name, bool requireAtLeastOne = false)
{
    return Attr{
        id, name, "", "", "",
        [](const QString &) { return QString{}; },
        std::nullopt,
        !requireAtLeastOne,   // optional
        std::nullopt,
        true,                 // isImage
        std::make_optional<std::function<QString(const QList<QSharedPointer<QImage>> &)>>(
            [requireAtLeastOne, name](const QList<QSharedPointer<QImage>> &imgs) -> QString {
                if (requireAtLeastOne && imgs.isEmpty()) {
                    return name + " requires at least one image";
                }
                return QString{};
            })
    };
}

QSharedPointer<QImage> makeImage(int w, int h, QColor color)
{
    auto img = QSharedPointer<QImage>::create(w, h, QImage::Format_RGB32);
    img->fill(color);
    return img;
}

// Returns the column index whose Qt::Horizontal header == name, or -1.
int findColumn(QSqlTableModel *model, const QString &name)
{
    for (int c = 0; c < model->columnCount(); ++c) {
        if (model->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString() == name) {
            return c;
        }
    }
    return -1;
}

// Returns the value at (row, colName) via data(DisplayRole).
QString cellText(QSqlTableModel *model, int row, const QString &colName)
{
    const int col = findColumn(model, colName);
    if (col < 0) {
        return QString{};
    }
    return model->data(model->index(row, col), Qt::DisplayRole).toString();
}

} // namespace

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------
class Test_DownloadedPagesTable : public QObject
{
    Q_OBJECT

private slots:
    // --- Constructor ---
    void test_constructor_creates_db_file();
    void test_constructor_initial_rowCount_is_zero();
    void test_constructor_loads_preexisting_rows();

    // --- recordPage() text ---
    void test_recordPage_inserts_row_and_increments_rowCount();
    void test_recordPage_text_values_readable_via_data();
    void test_recordPage_multiple_rows_sequential();
    void test_recordPage_throws_on_required_attr_missing_rowCount_unchanged();
    void test_recordPage_throws_on_cross_validation_failure_rowCount_unchanged();
    void test_recordPage_optional_attr_empty_succeeds();
    void test_recordPage_row_ids_are_non_empty_and_unique();

    // --- Column structure ---
    void test_model_columns_text_only();
    void test_model_columns_with_image_attr();

    // --- data() overrides ---
    void test_data_DisplayRole_hides_image_column();
    void test_data_EditRole_hides_image_column();
    void test_data_text_column_returns_value();
    void test_data_id_column_is_non_empty();
    void test_data_invalid_index_returns_null_variant();

    // --- imagesAt() ---
    void test_imagesAt_invalid_index_returns_empty_hash();
    void test_imagesAt_no_image_attrs_returns_empty_hash();
    void test_imagesAt_single_image_not_null_and_correct_count();
    void test_imagesAt_three_images_correct_count();
    void test_imagesAt_image_dimensions_preserved();
    void test_imagesAt_pixel_colors_preserved_for_rgb();
    void test_imagesAt_multiple_image_attributes_keys_and_counts();
    void test_imagesAt_empty_image_list_returns_nonnull_empty();
    void test_imagesAt_two_rows_have_independent_images();

    // --- Persistence ---
    void test_rows_persist_across_instance_recreation();
    void test_text_values_persist_across_instance_recreation();
    void test_images_persist_across_instance_recreation();

    // --- Image validation ---
    void test_recordPage_image_validation_failure_throws_rowCount_unchanged();
    void test_recordPage_image_validation_success_inserts_row();

    // --- Mixed text + image ---
    void test_mixed_text_and_image_round_trip();
};

// ===========================================================================
// Constructor
// ===========================================================================

void Test_DownloadedPagesTable::test_constructor_creates_db_file()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString dbPath = dir.path() + "/dl.db";
    QVERIFY(!QFile::exists(dbPath));

    StubDownloader dl("dl", {makeTextAttr("name", "Name")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);

    QVERIFY(QFile::exists(dbPath));
}

void Test_DownloadedPagesTable::test_constructor_initial_rowCount_is_zero()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeTextAttr("name", "Name")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);

    QCOMPARE(table.rowCount(), 0);
}

void Test_DownloadedPagesTable::test_constructor_loads_preexisting_rows()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<Attr> attrs = {makeTextAttr("name", "Name")};

    {
        StubDownloader dl("persist", attrs);
        DownloadedPagesTable table(QDir(dir.path()), &dl);
        table.recordPage({{"name", "row1"}});
        table.recordPage({{"name", "row2"}});
        QCOMPARE(table.rowCount(), 2);
    } // table + dl destroyed; db file stays

    StubDownloader dl2("persist", attrs);
    DownloadedPagesTable table2(QDir(dir.path()), &dl2);

    QCOMPARE(table2.rowCount(), 2);
    QVERIFY(!cellText(&table2, 0, "name").isEmpty());
}

// ===========================================================================
// recordPage() — text attributes
// ===========================================================================

void Test_DownloadedPagesTable::test_recordPage_inserts_row_and_increments_rowCount()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeTextAttr("name", "Name")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);

    QCOMPARE(table.rowCount(), 0);
    table.recordPage({{"name", "Alpha"}});
    QCOMPARE(table.rowCount(), 1);
    table.recordPage({{"name", "Beta"}});
    QCOMPARE(table.rowCount(), 2);
}

void Test_DownloadedPagesTable::test_recordPage_text_values_readable_via_data()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeTextAttr("title", "Title"), makeTextAttr("brand", "Brand")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);
    table.recordPage({{"title", "Whiskas Tuna"}, {"brand", "Whiskas"}});

    QVERIFY(findColumn(&table, "title") != -1);
    QVERIFY(findColumn(&table, "brand") != -1);
    QCOMPARE(cellText(&table, 0, "title"), QString("Whiskas Tuna"));
    QCOMPARE(cellText(&table, 0, "brand"), QString("Whiskas"));
}

void Test_DownloadedPagesTable::test_recordPage_multiple_rows_sequential()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeTextAttr("name", "Name")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);
    table.recordPage({{"name", "Alpha"}});
    table.recordPage({{"name", "Beta"}});
    table.recordPage({{"name", "Gamma"}});

    QCOMPARE(table.rowCount(), 3);

    // Collect all names; order is SQLite insertion order.
    QSet<QString> names;
    for (int r = 0; r < table.rowCount(); ++r) {
        names.insert(cellText(&table, r, "name"));
    }
    QVERIFY(names.contains("Alpha"));
    QVERIFY(names.contains("Beta"));
    QVERIFY(names.contains("Gamma"));
}

void Test_DownloadedPagesTable::test_recordPage_throws_on_required_attr_missing_rowCount_unchanged()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeTextAttr("title", "Title", /*required=*/true)});
    DownloadedPagesTable table(QDir(dir.path()), &dl);

    QCOMPARE(table.rowCount(), 0);

    bool threw = false;
    try {
        table.recordPage({{"title", ""}});
    } catch (const ExceptionWithTitleText &e) {
        threw = true;
        QVERIFY(!e.errorTitle().isEmpty());
        QVERIFY(!e.errorText().isEmpty());
    }
    QVERIFY(threw);
    QCOMPARE(table.rowCount(), 0);   // nothing written
}

void Test_DownloadedPagesTable::test_recordPage_throws_on_cross_validation_failure_rowCount_unchanged()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto crossVal = [](const QHash<QString, QString> &vals) -> QString {
        const int aCount = vals.value("col_a").split(';').size();
        const int bCount = vals.value("col_b").split(';').size();
        if (aCount != bCount) {
            return QString("col_a and col_b must have the same count");
        }
        return QString{};
    };

    StubDownloader dl("dl",
                      {makeTextAttr("col_a", "ColA"), makeTextAttr("col_b", "ColB")},
                      crossVal);
    DownloadedPagesTable table(QDir(dir.path()), &dl);

    QCOMPARE(table.rowCount(), 0);

    bool threw = false;
    try {
        table.recordPage({{"col_a", "x;y;z"}, {"col_b", "a;b"}});
    } catch (const ExceptionWithTitleText &e) {
        threw = true;
        QVERIFY(!e.errorText().isEmpty());
    }
    QVERIFY(threw);
    QCOMPARE(table.rowCount(), 0);

    // A matching record must succeed
    bool noThrow = true;
    try {
        table.recordPage({{"col_a", "x;y"}, {"col_b", "a;b"}});
    } catch (...) {
        noThrow = false;
    }
    QVERIFY(noThrow);
    QCOMPARE(table.rowCount(), 1);
}

void Test_DownloadedPagesTable::test_recordPage_optional_attr_empty_succeeds()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeTextAttr("name", "Name", true),
                             makeTextAttr("notes", "Notes", false)});
    DownloadedPagesTable table(QDir(dir.path()), &dl);

    bool noThrow = true;
    try {
        table.recordPage({{"name", "Fluffy"}, {"notes", ""}});
        table.recordPage({{"name", "Mochi"}});   // notes absent entirely
    } catch (...) {
        noThrow = false;
    }
    QVERIFY(noThrow);
    QCOMPARE(table.rowCount(), 2);
}

void Test_DownloadedPagesTable::test_recordPage_row_ids_are_non_empty_and_unique()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeTextAttr("name", "Name")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);
    table.recordPage({{"name", "A"}});
    table.recordPage({{"name", "B"}});
    table.recordPage({{"name", "C"}});

    const QString id0 = cellText(&table, 0, "id");
    const QString id1 = cellText(&table, 1, "id");
    const QString id2 = cellText(&table, 2, "id");

    QVERIFY(!id0.isEmpty());
    QVERIFY(!id1.isEmpty());
    QVERIFY(!id2.isEmpty());
    QVERIFY(id0 != id1);
    QVERIFY(id1 != id2);
    QVERIFY(id0 != id2);
}

// ===========================================================================
// Column structure
// ===========================================================================

void Test_DownloadedPagesTable::test_model_columns_text_only()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeTextAttr("title", "Title"), makeTextAttr("brand", "Brand")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);

    QVERIFY(findColumn(&table, "id")    != -1);
    QVERIFY(findColumn(&table, "title") != -1);
    QVERIFY(findColumn(&table, "brand") != -1);
    QCOMPARE(table.columnCount(), 3);   // id + title + brand
}

void Test_DownloadedPagesTable::test_model_columns_with_image_attr()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeTextAttr("title", "Title"), makeImgAttr("photos", "Photos")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);

    QVERIFY(findColumn(&table, "id")     != -1);
    QVERIFY(findColumn(&table, "title")  != -1);
    QVERIFY(findColumn(&table, "photos") != -1);
    QCOMPARE(table.columnCount(), 3);   // id + title + photos
}

// ===========================================================================
// data() overrides
// ===========================================================================

void Test_DownloadedPagesTable::test_data_DisplayRole_hides_image_column()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeImgAttr("photos", "Photos")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);
    table.recordPage({}, {{"photos", {makeImage(4, 4, Qt::red)}}});

    const int imgCol = findColumn(&table, "photos");
    QVERIFY(imgCol != -1);

    const QVariant v = table.data(table.index(0, imgCol), Qt::DisplayRole);
    QVERIFY(v.isNull() || !v.isValid() || v.toString().isEmpty());
}

void Test_DownloadedPagesTable::test_data_EditRole_hides_image_column()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeImgAttr("photos", "Photos")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);
    table.recordPage({}, {{"photos", {makeImage(4, 4, Qt::green)}}});

    const int imgCol = findColumn(&table, "photos");
    QVERIFY(imgCol != -1);

    const QVariant v = table.data(table.index(0, imgCol), Qt::EditRole);
    QVERIFY(v.isNull() || !v.isValid() || v.toString().isEmpty());
}

void Test_DownloadedPagesTable::test_data_text_column_returns_value()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeTextAttr("name", "Name")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);
    table.recordPage({{"name", "Whiskas"}});

    const int nameCol = findColumn(&table, "name");
    QVERIFY(nameCol != -1);
    QCOMPARE(table.data(table.index(0, nameCol), Qt::DisplayRole).toString(),
             QString("Whiskas"));
    QCOMPARE(table.data(table.index(0, nameCol), Qt::EditRole).toString(),
             QString("Whiskas"));
}

void Test_DownloadedPagesTable::test_data_id_column_is_non_empty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeTextAttr("name", "Name")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);
    table.recordPage({{"name", "Felix"}});

    const int idCol = findColumn(&table, "id");
    QVERIFY(idCol != -1);
    QVERIFY(!table.data(table.index(0, idCol), Qt::DisplayRole).toString().isEmpty());
}

void Test_DownloadedPagesTable::test_data_invalid_index_returns_null_variant()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeTextAttr("name", "Name")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);

    // Default-constructed QModelIndex is invalid
    const QVariant v1 = table.data(QModelIndex{}, Qt::DisplayRole);
    QVERIFY(!v1.isValid());

    // Row out of range (no rows inserted)
    const QVariant v2 = table.data(table.index(99, 0), Qt::DisplayRole);
    QVERIFY(!v2.isValid());
}

// ===========================================================================
// imagesAt()
// ===========================================================================

void Test_DownloadedPagesTable::test_imagesAt_invalid_index_returns_empty_hash()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeImgAttr("photos", "Photos")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);

    const auto result = table.imagesAt(QModelIndex{});
    QVERIFY(result.isEmpty());
}

void Test_DownloadedPagesTable::test_imagesAt_no_image_attrs_returns_empty_hash()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeTextAttr("name", "Name")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);
    table.recordPage({{"name", "Pedigree"}});

    const auto result = table.imagesAt(table.index(0, 0));
    QVERIFY(result.isEmpty());
    QCOMPARE(result.size(), 0);
}

void Test_DownloadedPagesTable::test_imagesAt_single_image_not_null_and_correct_count()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeImgAttr("photos", "Photos")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);
    table.recordPage({}, {{"photos", {makeImage(4, 4, Qt::red)}}});

    const auto result = table.imagesAt(table.index(0, 0));
    QVERIFY(result.contains("photos"));
    QVERIFY(result.value("photos") != nullptr);
    QCOMPARE(result.value("photos")->size(), 1);
}

void Test_DownloadedPagesTable::test_imagesAt_three_images_correct_count()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeImgAttr("photos", "Photos")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);

    QList<QSharedPointer<QImage>> imgs = {
        makeImage(4, 4, Qt::red),
        makeImage(4, 4, Qt::green),
        makeImage(4, 4, Qt::blue),
    };
    table.recordPage({}, {{"photos", imgs}});

    const auto result = table.imagesAt(table.index(0, 0));
    QVERIFY(result.contains("photos"));
    QCOMPARE(result.value("photos")->size(), 3);
}

void Test_DownloadedPagesTable::test_imagesAt_image_dimensions_preserved()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeImgAttr("photos", "Photos")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);
    table.recordPage({}, {{"photos", {makeImage(8, 6, Qt::cyan)}}});

    const auto result = table.imagesAt(table.index(0, 0));
    QVERIFY(result.contains("photos"));

    const auto &imgList = *result.value("photos");
    QCOMPARE(imgList.size(), 1);
    QVERIFY(!imgList[0].isNull());
    QCOMPARE(imgList[0].width(),  8);
    QCOMPARE(imgList[0].height(), 6);
}

void Test_DownloadedPagesTable::test_imagesAt_pixel_colors_preserved_for_rgb()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeImgAttr("photos", "Photos")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);

    // One row with three images: red, green, blue
    QList<QSharedPointer<QImage>> imgs = {
        makeImage(4, 4, Qt::red),
        makeImage(4, 4, Qt::green),
        makeImage(4, 4, Qt::blue),
    };
    table.recordPage({}, {{"photos", imgs}});

    const auto result = table.imagesAt(table.index(0, 0));
    QVERIFY(result.contains("photos"));

    const auto &imgList = *result.value("photos");
    QCOMPARE(imgList.size(), 3);

    // Red image
    QCOMPARE(QColor(imgList[0].pixel(0, 0)).red(),   255);
    QCOMPARE(QColor(imgList[0].pixel(0, 0)).green(),   0);
    QCOMPARE(QColor(imgList[0].pixel(0, 0)).blue(),    0);

    // Green image
    QCOMPARE(QColor(imgList[1].pixel(0, 0)).red(),     0);
    QCOMPARE(QColor(imgList[1].pixel(0, 0)).green(), 255);
    QCOMPARE(QColor(imgList[1].pixel(0, 0)).blue(),    0);

    // Blue image
    QCOMPARE(QColor(imgList[2].pixel(0, 0)).red(),     0);
    QCOMPARE(QColor(imgList[2].pixel(0, 0)).green(),   0);
    QCOMPARE(QColor(imgList[2].pixel(0, 0)).blue(),  255);
}

void Test_DownloadedPagesTable::test_imagesAt_multiple_image_attributes_keys_and_counts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeImgAttr("photos", "Photos"),
                             makeImgAttr("thumbs", "Thumbs")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);

    QHash<QString, QList<QSharedPointer<QImage>>> imageValues;
    imageValues["photos"] = {makeImage(4, 4, Qt::red)};
    imageValues["thumbs"] = {makeImage(2, 2, Qt::blue), makeImage(2, 2, Qt::green)};
    table.recordPage({}, imageValues);

    const auto result = table.imagesAt(table.index(0, 0));
    QCOMPARE(result.size(), 2);
    QVERIFY(result.contains("photos"));
    QVERIFY(result.contains("thumbs"));
    QCOMPARE(result.value("photos")->size(), 1);
    QCOMPARE(result.value("thumbs")->size(), 2);
}

void Test_DownloadedPagesTable::test_imagesAt_empty_image_list_returns_nonnull_empty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeImgAttr("photos", "Photos")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);
    table.recordPage({}, {{"photos", {}}});   // empty list

    const auto result = table.imagesAt(table.index(0, 0));
    QVERIFY(result.contains("photos"));
    QVERIFY(result.value("photos") != nullptr);
    QCOMPARE(result.value("photos")->size(), 0);
}

void Test_DownloadedPagesTable::test_imagesAt_two_rows_have_independent_images()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeImgAttr("photos", "Photos")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);
    table.recordPage({}, {{"photos", {makeImage(4, 4, Qt::red)}}});
    table.recordPage({}, {{"photos", {makeImage(4, 4, Qt::blue),
                                      makeImage(4, 4, Qt::blue)}}});

    const auto r0 = table.imagesAt(table.index(0, 0));
    const auto r1 = table.imagesAt(table.index(1, 0));

    QVERIFY(r0.value("photos") != nullptr);
    QVERIFY(r1.value("photos") != nullptr);
    QCOMPARE(r0.value("photos")->size(), 1);
    QCOMPARE(r1.value("photos")->size(), 2);

    // Row 0: red pixel
    QCOMPARE(QColor(r0.value("photos")->at(0).pixel(0, 0)).red(), 255);
    QCOMPARE(QColor(r0.value("photos")->at(0).pixel(0, 0)).blue(),  0);

    // Row 1: blue pixel
    QCOMPARE(QColor(r1.value("photos")->at(0).pixel(0, 0)).red(),   0);
    QCOMPARE(QColor(r1.value("photos")->at(0).pixel(0, 0)).blue(), 255);
}

// ===========================================================================
// Persistence across instance recreation
// ===========================================================================

void Test_DownloadedPagesTable::test_rows_persist_across_instance_recreation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<Attr> attrs = {makeTextAttr("name", "Name")};

    {
        StubDownloader dl("persist", attrs);
        DownloadedPagesTable table(QDir(dir.path()), &dl);
        table.recordPage({{"name", "A"}});
        table.recordPage({{"name", "B"}});
        table.recordPage({{"name", "C"}});
    }

    StubDownloader dl2("persist", attrs);
    DownloadedPagesTable table2(QDir(dir.path()), &dl2);

    QCOMPARE(table2.rowCount(), 3);
    QVERIFY(!cellText(&table2, 0, "id").isEmpty());
}

void Test_DownloadedPagesTable::test_text_values_persist_across_instance_recreation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<Attr> attrs = {makeTextAttr("name", "Name")};

    {
        StubDownloader dl("persist", attrs);
        DownloadedPagesTable table(QDir(dir.path()), &dl);
        table.recordPage({{"name", "Persistent"}});
    }

    StubDownloader dl2("persist", attrs);
    DownloadedPagesTable table2(QDir(dir.path()), &dl2);

    QCOMPARE(table2.rowCount(), 1);
    QCOMPARE(cellText(&table2, 0, "name"), QString("Persistent"));
}

void Test_DownloadedPagesTable::test_images_persist_across_instance_recreation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<Attr> attrs = {makeImgAttr("photos", "Photos")};

    {
        StubDownloader dl("persist", attrs);
        DownloadedPagesTable table(QDir(dir.path()), &dl);
        QList<QSharedPointer<QImage>> imgs = {makeImage(4, 4, Qt::red),
                                               makeImage(4, 4, Qt::red)};
        table.recordPage({}, {{"photos", imgs}});
    }

    StubDownloader dl2("persist", attrs);
    DownloadedPagesTable table2(QDir(dir.path()), &dl2);

    QCOMPARE(table2.rowCount(), 1);

    const auto result = table2.imagesAt(table2.index(0, 0));
    QVERIFY(result.contains("photos"));
    QVERIFY(result.value("photos") != nullptr);
    QCOMPARE(result.value("photos")->size(), 2);

    const auto &imgList = *result.value("photos");
    QCOMPARE(QColor(imgList[0].pixel(0, 0)).red(), 255);
    QCOMPARE(QColor(imgList[0].pixel(0, 0)).green(),  0);
    QCOMPARE(QColor(imgList[0].pixel(0, 0)).blue(),   0);
}

// ===========================================================================
// Image validation
// ===========================================================================

void Test_DownloadedPagesTable::test_recordPage_image_validation_failure_throws_rowCount_unchanged()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Require at least one image
    StubDownloader dl("dl", {makeImgAttr("photos", "Photos", /*requireAtLeastOne=*/true)});
    DownloadedPagesTable table(QDir(dir.path()), &dl);

    QCOMPARE(table.rowCount(), 0);

    bool threw = false;
    try {
        table.recordPage({}, {{"photos", {}}});   // empty list → validation fails
    } catch (const ExceptionWithTitleText &e) {
        threw = true;
        QVERIFY(!e.errorText().isEmpty());
    }
    QVERIFY(threw);
    QCOMPARE(table.rowCount(), 0);
}

void Test_DownloadedPagesTable::test_recordPage_image_validation_success_inserts_row()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeImgAttr("photos", "Photos", /*requireAtLeastOne=*/true)});
    DownloadedPagesTable table(QDir(dir.path()), &dl);

    bool noThrow = true;
    try {
        table.recordPage({}, {{"photos", {makeImage(4, 4, Qt::red)}}});
    } catch (...) {
        noThrow = false;
    }
    QVERIFY(noThrow);
    QCOMPARE(table.rowCount(), 1);
}

// ===========================================================================
// Mixed text + image round-trip
// ===========================================================================

void Test_DownloadedPagesTable::test_mixed_text_and_image_round_trip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    StubDownloader dl("dl", {makeTextAttr("name", "Name"), makeImgAttr("photos", "Photos")});
    DownloadedPagesTable table(QDir(dir.path()), &dl);

    table.recordPage({{"name", "Tabby Cat Food"}},
                     {{"photos", {makeImage(4, 4, Qt::red)}}});

    QCOMPARE(table.rowCount(), 1);

    // Text column is visible
    const int nameCol = findColumn(&table, "name");
    QVERIFY(nameCol != -1);
    QCOMPARE(table.data(table.index(0, nameCol), Qt::DisplayRole).toString(),
             QString("Tabby Cat Food"));

    // Image column is hidden
    const int imgCol = findColumn(&table, "photos");
    QVERIFY(imgCol != -1);
    const QVariant imgDisplay = table.data(table.index(0, imgCol), Qt::DisplayRole);
    QVERIFY(imgDisplay.isNull() || !imgDisplay.isValid() || imgDisplay.toString().isEmpty());

    // Image data retrieved correctly
    const auto result = table.imagesAt(table.index(0, 0));
    QVERIFY(result.contains("photos"));
    QVERIFY(result.value("photos") != nullptr);
    QCOMPARE(result.value("photos")->size(), 1);
    QCOMPARE(QColor(result.value("photos")->at(0).pixel(0, 0)).red(), 255);
    QCOMPARE(QColor(result.value("photos")->at(0).pixel(0, 0)).green(),  0);
    QCOMPARE(QColor(result.value("photos")->at(0).pixel(0, 0)).blue(),   0);
}

QTEST_MAIN(Test_DownloadedPagesTable)
#include "test_downloaded_pages_table.moc"
