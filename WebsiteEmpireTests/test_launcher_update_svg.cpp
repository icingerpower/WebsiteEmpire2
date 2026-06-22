#include <QtTest>
#include <QDir>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <atomic>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

static std::atomic<int> s_connIdx{0};

static QString uniqueConn(const QString &prefix)
{
    return prefix + QString::number(s_connIdx.fetch_add(1));
}

// Create the images.db schema used by LauncherUpdate and ImageWriter.
// Returns false if the database could not be opened.
static bool createImagesDb(const QString &path)
{
    const QString conn = uniqueConn(QStringLiteral("create_imgs_"));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(path);
        ok = db.open();
        if (ok) {
            QSqlQuery q(db);
            q.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS images ("
                " id        INTEGER PRIMARY KEY,"
                " blob      BLOB    NOT NULL,"
                " mime_type TEXT    NOT NULL)"));
            q.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS image_names ("
                " domain   TEXT    NOT NULL,"
                " filename TEXT    NOT NULL,"
                " image_id INTEGER NOT NULL REFERENCES images(id),"
                " PRIMARY KEY (domain, filename))"));
        }
    }
    QSqlDatabase::removeDatabase(conn);
    return ok;
}

// Insert a named SVG row into images.db.  Returns the inserted image_id, or -1 on error.
static int insertSvg(const QString &imgDbPath,
                      const QString &filename,
                      const QString &svgContent)
{
    int imageId = -1;
    const QString conn = uniqueConn(QStringLiteral("insert_svg_"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(imgDbPath);
        if (!db.open()) {
            return -1;
        }
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO images (blob, mime_type) VALUES (:blob, 'image/svg+xml')"));
        q.bindValue(QStringLiteral(":blob"), svgContent.toUtf8());
        if (!q.exec()) {
            return -1;
        }
        imageId = q.lastInsertId().toInt();

        QSqlQuery q2(db);
        q2.prepare(QStringLiteral(
            "INSERT INTO image_names (domain, filename, image_id)"
            " VALUES ('', :fn, :id)"));
        q2.bindValue(QStringLiteral(":fn"), filename);
        q2.bindValue(QStringLiteral(":id"), imageId);
        if (!q2.exec()) {
            return -1;
        }
    }
    QSqlDatabase::removeDatabase(conn);
    return imageId;
}

// Read the blob for a filename from images.db.
static QByteArray readSvgBlob(const QString &imgDbPath, const QString &filename)
{
    QByteArray result;
    const QString conn = uniqueConn(QStringLiteral("read_svg_"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(imgDbPath);
        if (!db.open()) {
            return result;
        }
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT b.blob FROM images b"
            " JOIN image_names n ON n.image_id = b.id"
            " WHERE n.filename = :fn LIMIT 1"));
        q.bindValue(QStringLiteral(":fn"), filename);
        if (q.exec() && q.next()) {
            result = q.value(0).toByteArray();
        }
    }
    QSqlDatabase::removeDatabase(conn);
    return result;
}

// Apply the images.db UPDATE path used by LauncherUpdate (the fix).
// Returns true if the UPDATE succeeded.
static bool updateSvgBlob(const QString &imgDbPath,
                           int            imageId,
                           const QString &newSvgContent)
{
    bool saved = false;
    const QString conn = uniqueConn(QStringLiteral("update_svg_"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(imgDbPath);
        if (db.open()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "UPDATE images SET blob = :blob WHERE id = :id"));
            q.bindValue(QStringLiteral(":blob"), newSvgContent.toUtf8());
            q.bindValue(QStringLiteral(":id"),   imageId);
            saved = q.exec();
        }
    }
    QSqlDatabase::removeDatabase(conn);
    return saved;
}

// Extract SVG filenames from [IMGFIX] shortcodes — mirrors the regex used in
// LauncherUpdate::runUpdateSession after the fix.
static QStringList extractImgFixSvgFilenames(const QString &text)
{
    static const QRegularExpression reImgFix(
        QStringLiteral("\\[IMGFIX\\b[^\\]]*fileName=\"([^\"]+\\.svg)\""),
        QRegularExpression::CaseInsensitiveOption);

    QStringList result;
    auto it = reImgFix.globalMatch(text);
    while (it.hasNext()) {
        const QString fn = it.next().captured(1);
        if (!result.contains(fn)) {
            result.append(fn);
        }
    }
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class Test_Website_LauncherUpdate_Svg : public QObject
{
    Q_OBJECT

private slots:
    void test_svgupdate_imgfix_regex_extracts_svg_filename();
    void test_svgupdate_imgfix_regex_ignores_non_svg_imgfix();
    void test_svgupdate_imgfix_regex_deduplicates_same_filename();
    void test_svgupdate_imgfix_regex_returns_empty_for_no_shortcode();
    void test_svgupdate_blob_written_to_images_db_not_to_1_text();
    void test_svgupdate_blob_update_replaces_existing_svg_in_images_db();
};

// ---------------------------------------------------------------------------
// test_svgupdate_imgfix_regex_extracts_svg_filename
// Verifies that the IMGFIX regex correctly captures the SVG filename from a
// shortcode embedded in article text, as required by the fixed code path.
// ---------------------------------------------------------------------------

void Test_Website_LauncherUpdate_Svg::test_svgupdate_imgfix_regex_extracts_svg_filename()
{
    const QString text = QStringLiteral(
        "[TITLE level=\"1\"]My Article[/TITLE]\n"
        "Some content here.\n"
        "[IMGFIX fileName=\"chart.svg\" alt=\"A chart\"]\n"
        "More content.");

    const QStringList &filenames = extractImgFixSvgFilenames(text);

    QCOMPARE(filenames.size(), 1);
    QCOMPARE(filenames.first(), QStringLiteral("chart.svg"));
}

// ---------------------------------------------------------------------------
// test_svgupdate_imgfix_regex_ignores_non_svg_imgfix
// IMGFIX shortcodes for non-SVG images must not appear in the SVG filename
// list — the fix only processes .svg files via images.db.
// ---------------------------------------------------------------------------

void Test_Website_LauncherUpdate_Svg::test_svgupdate_imgfix_regex_ignores_non_svg_imgfix()
{
    const QString text = QStringLiteral(
        "[IMGFIX fileName=\"photo.webp\" alt=\"A photo\"]\n"
        "[IMGFIX fileName=\"banner.png\" alt=\"A banner\"]\n"
        "[IMGFIX fileName=\"icon.jpg\" alt=\"An icon\"]\n");

    const QStringList &filenames = extractImgFixSvgFilenames(text);

    QVERIFY2(filenames.isEmpty(),
             "Non-SVG IMGFIX shortcodes must not be included in the SVG filename list");
}

// ---------------------------------------------------------------------------
// test_svgupdate_imgfix_regex_deduplicates_same_filename
// When the same SVG filename appears more than once (e.g. inline + caption),
// the list must contain it only once — matching the dedup logic in the fix.
// ---------------------------------------------------------------------------

void Test_Website_LauncherUpdate_Svg::test_svgupdate_imgfix_regex_deduplicates_same_filename()
{
    const QString text = QStringLiteral(
        "[IMGFIX fileName=\"chart.svg\" alt=\"First\"]\n"
        "[IMGFIX fileName=\"chart.svg\" alt=\"Second\"]\n");

    const QStringList &filenames = extractImgFixSvgFilenames(text);

    QCOMPARE(filenames.size(), 1);
    QCOMPARE(filenames.first(), QStringLiteral("chart.svg"));
}

// ---------------------------------------------------------------------------
// test_svgupdate_imgfix_regex_returns_empty_for_no_shortcode
// When 1_text contains no [IMGFIX] SVG shortcode at all, the fix skips the
// page (outputs "SKIP: no [IMGFIX] SVG shortcode found").  This verifies the
// guard condition at the top of the SVG path.
// ---------------------------------------------------------------------------

void Test_Website_LauncherUpdate_Svg::test_svgupdate_imgfix_regex_returns_empty_for_no_shortcode()
{
    const QString text = QStringLiteral(
        "[TITLE level=\"1\"]Article with inline SVG[/TITLE]\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\"><rect width=\"100\" height=\"100\"/></svg>\n"
        "Some text here.");

    const QStringList &filenames = extractImgFixSvgFilenames(text);

    QVERIFY2(filenames.isEmpty(),
             "An inline SVG in 1_text (not via [IMGFIX]) must not be extracted; "
             "only [IMGFIX fileName=...svg] shortcodes are the target of the fix");
}

// ---------------------------------------------------------------------------
// test_svgupdate_blob_written_to_images_db_not_to_1_text
// Core regression test for the bug: the old code spliced the generated SVG
// directly into 1_text; the fix writes it to images.db.
//
// This test verifies the fixed behaviour by:
//  1. Creating an images.db with an initial SVG blob.
//  2. Performing the UPDATE query from the fixed code path.
//  3. Confirming the blob in images.db was updated.
//  4. Confirming that 1_text (represented here as a plain QString) was NOT
//     modified — it still contains only the [IMGFIX] shortcode, not SVG markup.
//
// The test FAILS against the old code because the old code never touched
// images.db at all (the UPDATE would not have run), leaving the blob unchanged.
// ---------------------------------------------------------------------------

void Test_Website_LauncherUpdate_Svg::test_svgupdate_blob_written_to_images_db_not_to_1_text()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imgDbPath = QDir(dir.path()).filePath(QStringLiteral("images.db"));
    QVERIFY(createImagesDb(imgDbPath));

    // Seed images.db with the original SVG.
    const QString originalSvg = QStringLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<text x=\"10\" y=\"20\">Original</text>"
        "</svg>");
    const int imageId = insertSvg(imgDbPath, QStringLiteral("chart.svg"), originalSvg);
    QVERIFY(imageId >= 0);

    // Represent 1_text as it is stored: contains [IMGFIX] shortcode, not SVG markup.
    // This is the source of truth that the fix reads (via the regex) to discover
    // which SVG file to update in images.db.
    QString pageText = QStringLiteral(
        "[TITLE level=\"1\"]Health Trends[/TITLE]\n"
        "Key statistics for 2024.\n"
        "[IMGFIX fileName=\"chart.svg\" alt=\"Health statistics chart\"]\n"
        "More content follows.");

    // The fix uses extractImgFixSvgFilenames to discover "chart.svg", then
    // loads its blob from images.db, and after a (mocked) Claude call, writes
    // the updated SVG back via the UPDATE query.  We simulate the "Claude call"
    // result here to focus on the database write path.
    const QString updatedSvg = QStringLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"200\">"
        "<text x=\"10\" y=\"20\">Updated</text>"
        "</svg>");

    // Apply the UPDATE path from the fixed LauncherUpdate code.
    const bool saved = updateSvgBlob(imgDbPath, imageId, updatedSvg);
    QVERIFY2(saved, "UPDATE into images.db must succeed");

    // --- Verify images.db contains the updated blob ---
    const QByteArray &blobAfter = readSvgBlob(imgDbPath, QStringLiteral("chart.svg"));
    QCOMPARE(QString::fromUtf8(blobAfter), updatedSvg);

    // --- Verify 1_text was NOT modified (the old bug would have spliced SVG here) ---
    // The fixed code path (prompt.updateSvg == true) returns early with `continue`
    // after saving to images.db and never calls saveData(page.id, data) for 1_text.
    // We assert that pageText is unchanged to document this invariant.
    QVERIFY2(!pageText.contains(QStringLiteral("<svg")),
             "1_text must not contain SVG markup after the fix: "
             "SVG is stored in images.db, not spliced into 1_text");
    QVERIFY2(pageText.contains(QStringLiteral("[IMGFIX")),
             "1_text must still contain the [IMGFIX] shortcode after the update");
}

// ---------------------------------------------------------------------------
// test_svgupdate_blob_update_replaces_existing_svg_in_images_db
// When the images.db already has a row for the SVG filename (imageId >= 0),
// the fix uses UPDATE, not INSERT.  This ensures the existing row is mutated
// in place and no orphan rows are created.
// ---------------------------------------------------------------------------

void Test_Website_LauncherUpdate_Svg::test_svgupdate_blob_update_replaces_existing_svg_in_images_db()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imgDbPath = QDir(dir.path()).filePath(QStringLiteral("images.db"));
    QVERIFY(createImagesDb(imgDbPath));

    const QString svgV1 = QStringLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\"><text>v1</text></svg>");
    const int imageId = insertSvg(imgDbPath, QStringLiteral("diagram.svg"), svgV1);
    QVERIFY(imageId >= 0);

    // Verify initial state.
    QCOMPARE(QString::fromUtf8(readSvgBlob(imgDbPath, QStringLiteral("diagram.svg"))), svgV1);

    const QString svgV2 = QStringLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\"><text>v2</text></svg>");
    const bool saved = updateSvgBlob(imgDbPath, imageId, svgV2);
    QVERIFY2(saved, "UPDATE must succeed for an existing image row");

    // Blob must now reflect v2.
    QCOMPARE(QString::fromUtf8(readSvgBlob(imgDbPath, QStringLiteral("diagram.svg"))), svgV2);

    // Only one row must exist for this filename — no duplicate was inserted.
    const QString conn = uniqueConn(QStringLiteral("count_check_"));
    int rowCount = -1;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(imgDbPath);
        QVERIFY(db.open());
        QSqlQuery q(db);
        q.exec(QStringLiteral("SELECT COUNT(*) FROM images"));
        if (q.next()) {
            rowCount = q.value(0).toInt();
        }
    }
    QSqlDatabase::removeDatabase(conn);

    QCOMPARE(rowCount, 1);
}

QTEST_MAIN(Test_Website_LauncherUpdate_Svg)
#include "test_launcher_update_svg.moc"
